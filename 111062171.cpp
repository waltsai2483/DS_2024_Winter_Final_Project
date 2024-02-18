#define FILE_EXTENSION ".txt"
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <iostream>
#include <chrono>
#include <omp.h>
#include <unordered_map>

using namespace std;

// Utility Func

vector<string> word_parse(vector<string> tmp_string)
{
    vector<string> parsed_str;
    for (auto &word : tmp_string)
    {
        string new_str;
        for (auto &ch : word)
        {
            if (isalpha(ch))
                new_str.push_back(ch);
        }
        parsed_str.push_back(new_str);
    }
    return parsed_str;
}

vector<string> split(const string &str, const string &delim)
{
    vector<string> res;
    if (str.empty())
        return res;

    char *strs = new char[str.length() + 1];
    strcpy(strs, str.c_str());

    char *d = new char[delim.length() + 1];
    strcpy(d, delim.c_str());

    char *p = strtok(strs, d);
    while (p)
    {
        string s = p;
        res.push_back(s);
        p = strtok(nullptr, d);
    }
    delete[] strs;
    delete[] d;
    return res;
}

// Trie

int trie_tag = 0;

struct node
{
    node *children[26];
    int tag;
    bool end;
    ~node()
    {
        for (int i = 0; i < 26; i++)
        {
            if (children[i])
            {
                delete children[i];
            }
        }
    }

    node *move(string str)
    {
        node *curr = this;
        for (auto it = str.begin(); it != str.end(); it++)
        {
            curr = curr->children[tolower(*it) - 'a'];
            if (!curr || curr->tag != trie_tag)
                return nullptr;
        }
        return curr;
    }

    node *rmove(string str)
    {
        node *curr = this;
        for (auto it = str.rbegin(); it != str.rend(); it++)
        {
            curr = curr->children[tolower(*it) - 'a'];
            if (!curr || curr->tag != trie_tag)
                return nullptr;
        }
        return curr;
    }
};

class trie
{
public:
    node *root;
    trie()
    {
        root = new node();
    }
    ~trie()
    {
        delete root;
    }
    void insert(string str)
    {
        node *curr = root;
        for (auto it = str.begin(); it != str.end(); it++)
        {
            int idx = tolower(*it) - 'a';
            if (!curr->children[idx])
            {
                curr->children[idx] = new node();
            }
            if (curr->children[idx]->tag != trie_tag)
            {
                curr->children[idx]->end = false;
                curr->children[idx]->tag = trie_tag;
            }
            curr = curr->children[idx];
        }
        curr->end = true;
    }

    void rinsert(string str)
    {
        node *curr = root;
        for (auto it = str.rbegin(); it != str.rend(); it++)
        {
            int idx = tolower(*it) - 'a';
            if (!curr->children[idx])
            {
                curr->children[idx] = new node();
            }
            curr->children[idx]->tag = trie_tag;
            curr = curr->children[idx];
        }
        curr->end = true;
    }
};

trie prefix_trie;
trie suffix_trie;
string titles[10000];

bool trie_gen(vector<string> content, int idx)
{
    trie_tag = idx;
#pragma omp parallel shared(prefix_trie, suffix_trie)
    {
        vector<string> words;
#pragma omp for
        for (const string &line : content)
        {
            if (line.empty())
            {
                continue;
            }
#pragma omp critical
            words = split(line, " ");

            words = word_parse(words);
            for (string word : words)
            {
                prefix_trie.insert(word);
                suffix_trie.rinsert(word);
            }
        }
    }

    return false;
}

bool wildcard(const string &word, int index, node *node)
{
    if (index == word.length())
        return node->end;
    if (word[index] == '*')
    {
        if (index == word.length() - 1)
        {
            return true;
        }

        if (wildcard(word, index + 1, node))
            return true;
        for (int i = 0; i < 26; i++)
        {
            if (node->children[i] && node->children[i]->tag == trie_tag)
                if (wildcard(word, index, node->children[i]))
                    return true;
        }
        return false;
    }
    short curr = tolower(word[index]) - 'a';
    if (!node->children[curr] || node->children[curr]->tag != trie_tag)
    {
        return false;
    }
    else
    {
        return wildcard(word, index + 1, node->children[curr]);
    }
}

bool run_single(string keyword)
{
    if (isalpha(keyword[0]))
    {
        return prefix_trie.root->move(keyword);
    }
    string str = keyword.substr(1, keyword.length() - 2);
    if (keyword[0] == '*')
    {
        return suffix_trie.root->rmove(str);
    }
    if (keyword[0] == '"')
    {
        node *temp = prefix_trie.root->move(str);
        return temp && temp->end;
    }
    if (keyword[0] == '<')
    {
        return wildcard(str, 0, prefix_trie.root);
    }
    return false;
}

bool run_query(vector<string> query)
{
    bool matched = run_single(query[0]);
    for (int i = 2; i < query.size(); i += 2)
    {
        if (query[i - 1][0] == '/' == matched)
            continue;
        switch (query[i - 1][0])
        {
            case '/':
            case '+':
                matched = run_single(query[i]);
                break;
            case '-':
                matched = !run_single(query[i]);
                break;
        }
    }
    return matched;
}

// Main function

int main(int argc, char *argv[])
{
    auto time_start = chrono::high_resolution_clock::now();
    string data_dir = argv[1] + string("/");
    string query_file = string(argv[2]);
    string output_file = string(argv[3]);

    ifstream fi;
    string str;
    vector<vector<string>> queries;
    fi.open(query_file, ios::in);
    while (getline(fi, str))
    {
        queries.push_back(split(str, " "));
    }
    fi.close();

    const int ESSAY_LOAD = 1000;
    vector<string> contents[ESSAY_LOAD];
    vector<int> matched_essays[queries.size()];

    for (int i = 0; i < 10000; i += ESSAY_LOAD)
    {
        int trie_count = 0;
        int min_fail = i + ESSAY_LOAD;
#pragma omp parallel for reduction(+ : trie_count) shared(contents)
        for (int j = i; j < min_fail; j++)
        {
        	ifstream tfi; string str2;
            tfi.open(data_dir + to_string(j) + ".txt", ios::in);

            if (tfi.fail())
            {
            	#pragma omp critical
            	min_fail = min(min_fail, j);
                continue;
            }
            trie_count++;
            bool flag = true;
            contents[j % ESSAY_LOAD].clear();
            while (getline(tfi, str2))
            {
                if (flag)
                {
                    titles[j] = str2;
                    flag = false;
                }
				contents[j % ESSAY_LOAD].push_back(str2);
            }
            tfi.close();
        }

        if (trie_count == 0)
        {
            break;
        }

        for (int j = 0; j < trie_count; j++)
        {
            trie_gen(contents[j], i + j);
#pragma omp parallel for shared(matched_essays)
            for (int k = 0; k < queries.size(); k++)
            {
                if (run_query(queries[k]))
                {
                    matched_essays[k].push_back(i + j);
                }
            }
        }
    }

    ofstream fo;
    fo.open(output_file, ios::out);
    for (int i = 0; i < queries.size(); i++)
    {
        if (matched_essays[i].empty())
        {
            fo << "Not Found!\n";
        }
        else
        {
            for (int essay : matched_essays[i])
            {
                fo << titles[essay] << "\n";
            }
        }
    }
    fo.close();
    cout << "Elapsed time: " << chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now()-time_start).count() << " ms\n";
    return 0;
}
