#include <iostream>
#include <string>
#include <vector>
#include <random>

#include "process_queries.h"
#include "search_server.h"
#include "request_queue.h"
#include "document.h"
#include "paginator.h"
#include "log_duration.h"
//#include "remove_duplicates.h"

using namespace std;

void PrintDocument(const Document& document) {
    cout << "{ "s
         << "document_id = "s << document.id << ", "s
         << "relevance = "s << document.relevance << ", "s
         << "rating = "s << document.rating << " }"s << endl;
}

void PrintMatchDocumentResult(int document_id, const vector<string_view>& words, DocumentStatus status) {
    cout << "{ "s
         << "document_id = "s << document_id << ", "s
         << "status = "s << static_cast<int>(status) << ", "s
         << "words ="s;
    for (const auto word : words) {
        cout << ' ' << word;
    }
    cout << "}"s << endl;
}

void AddDocument(SearchServer& search_server, int document_id, const string& document, DocumentStatus status,
                 const vector<int>& ratings) {
    try {
        search_server.AddDocument(document_id, document, status, ratings);
    } catch (const invalid_argument& e) {
        cout << "Ошибка добавления документа "s << document_id << ": "s << e.what() << endl;
    }
}

void AddDocument(SearchServer& search_server, int document_id, const string_view document, DocumentStatus status,
                 const vector<int>& ratings) {
    AddDocument(search_server, document_id, static_cast<std::string>(document), status,
                     ratings);
}

void AddDocument(SearchServer& search_server, int document_id, const char* document, DocumentStatus status,
                 const vector<int>& ratings) {
    AddDocument(search_server, document_id, static_cast<std::string>(document), status,
                     ratings);
}

void FindTopDocuments(const SearchServer& search_server, const string& raw_query) {
    cout << "Search results by query: "s << raw_query << endl;
    try {
        for (const Document& document : search_server.FindTopDocuments(raw_query)) {
            PrintDocument(document);
        }
    } catch (const invalid_argument& e) {
        cout << "Error search: "s << e.what() << endl;
    }
}

void MatchDocuments(const SearchServer& search_server, const string& query) {
    try {
        cout << "Matching documents by query: "s << query << endl;
        for (const auto document_id : search_server) {
            const auto [words, status] = search_server.MatchDocument(query, document_id);
            PrintMatchDocumentResult(document_id, words, status);
        }
    } catch (const invalid_argument& e) {
        cout << "Error matching documents by query "s << query << ": "s << e.what() << endl;
    }
}

string GenerateWord(mt19937& generator, int max_length) {
    const int length = uniform_int_distribution(1, max_length)(generator);
    string word;
    word.reserve(length);
    for (int i = 0; i < length; ++i) {
        word.push_back(uniform_int_distribution('a', 'z')(generator));
    }
    return word;
}

vector<string> GenerateDictionary(mt19937& generator, int word_count, int max_length) {
    vector<string> words;
    words.reserve(word_count);
    for (int i = 0; i < word_count; ++i) {
        words.push_back(GenerateWord(generator, max_length));
    }
    words.erase(unique(words.begin(), words.end()), words.end());
    return words;
}

string GenerateQuery(mt19937& generator, const vector<string>& dictionary, int word_count, double minus_prob = 0) {
    string query;
    for (int i = 0; i < word_count; ++i) {
        if (!query.empty()) {
            query.push_back(' ');
        }
        if (uniform_real_distribution<>(0, 1)(generator) < minus_prob) {
            query.push_back('-');
        }
        query += dictionary[uniform_int_distribution<int>(0, dictionary.size() - 1)(generator)];
    }
    return query;
}

vector<string> GenerateQueries(mt19937& generator, const vector<string>& dictionary, int query_count, int max_word_count) {
    vector<string> queries;
    queries.reserve(query_count);
    for (int i = 0; i < query_count; ++i) {
        queries.push_back(GenerateQuery(generator, dictionary, max_word_count));
    }
    return queries;
}

template <typename ExecutionPolicy>
void Test1(string_view mark, const SearchServer& search_server, const vector<string>& queries, ExecutionPolicy&& policy) {
    LOG_DURATION(mark);
    double total_relevance = 0;
    for (const string_view query : queries) {
        for (const auto& document : search_server.FindTopDocuments(policy, query)) {
            total_relevance += document.relevance;
        }
    }
    cout << total_relevance << endl;
}

#define TEST1(policy) Test1(#policy, search_server, queries, execution::policy)


template <typename ExecutionPolicy>
void Test(string_view mark, SearchServer search_server, const string& query, ExecutionPolicy&& policy) {
    LOG_DURATION(mark);
    const int document_count = search_server.GetDocumentCount();
    int word_count = 0;
    for (int id = 0; id < document_count; ++id) {
        const auto [words, status] = search_server.MatchDocument(policy, query, id);
        word_count += words.size();
    }
    cout << word_count << endl;
}

#define TEST(policy) Test(#policy, search_server, query, execution::policy)
/*
template <typename QueriesProcessor>
void Test(string_view mark, QueriesProcessor processor, const SearchServer& search_server, const vector<string>& queries) {
    LOG_DURATION(mark);
    const auto documents_lists = processor(search_server, queries);
}

#define TEST(processor) Test(#processor, processor, search_server, queries)


template <typename ExecutionPolicy>
void Test(string_view mark, SearchServer search_server, ExecutionPolicy&& policy) {
    LOG_DURATION(mark);
    const int document_count = search_server.GetDocumentCount();
    for (int id = 0; id < document_count; ++id) {
        search_server.RemoveDocument(policy, id);
    }
    cout << search_server.GetDocumentCount() << endl;
}

#define TEST(mode) Test(#mode, search_server, execution::mode)
*/


int main() {

cout << "FindTopDocuments"s << endl;
{
    SearchServer search_server("and with"s);

    int id = 0;
    for (
        const string& text : {
            "white cat and yellow hat"s,
            "curly cat curly tail"s,
            "nasty dog with big eyes"s,
            "nasty pigeon john"s,
        }
    ) {
        search_server.AddDocument(++id, text, DocumentStatus::ACTUAL, {1, 2});
    }

    cout << "SEQ"s << endl;
    cout << "ACTUAL by default:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments("curly nasty cat"s)) {
        PrintDocument(document);
    }
    cout << "BANNED:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments(execution::seq, "curly nasty cat"s, DocumentStatus::BANNED)) {
        PrintDocument(document);
    }

    cout << "Even ids:"s << endl;
    // параллельная версия
    for (const Document& document : search_server.FindTopDocuments(execution::seq, "curly nasty cat"s, [](int document_id, DocumentStatus status, int rating) { return document_id % 2 == 0; })) {
        PrintDocument(document);
    }

    cout << "PAR"s << endl;
    cout << "ACTUAL by default:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments(execution::par, "curly nasty cat"s)) {
        PrintDocument(document);
    }
    cout << "BANNED:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments(execution::par, "curly nasty cat"s, DocumentStatus::BANNED)) {
        PrintDocument(document);
    }

    cout << "Even ids:"s << endl;
    // параллельная версия
    for (const Document& document : search_server.FindTopDocuments(execution::par, "curly nasty cat"s, [](int document_id, DocumentStatus status, int rating) { return document_id % 2 == 0; })) {
        PrintDocument(document);
    }
}

{
    mt19937 generator;

    const auto dictionary = GenerateDictionary(generator, 1000, 10);
    const auto documents = GenerateQueries(generator, dictionary, 10'000, 70);

    SearchServer search_server(dictionary[0]);
    for (size_t i = 0; i < documents.size(); ++i) {
        search_server.AddDocument(i, documents[i], DocumentStatus::ACTUAL, {1, 2, 3});
    }

    const auto queries = GenerateQueries(generator, dictionary, 100, 70);

    TEST1(seq);
    TEST1(par);
}
cout << endl;
cout << "MatchDocument"s << endl;
    {
        //std::string_view stop_words("and with");
        SearchServer search_server(std::string_view("and with"));

        //AddDocument(search_server, 1, std::string_view("funny pet and nasty rat"), DocumentStatus::ACTUAL, {1, 2});
        AddDocument(search_server, 1, "funny pet and nasty rat", DocumentStatus::ACTUAL, {1, 2});
        AddDocument(search_server, 2, std::string_view("funny pet with curly hair"), DocumentStatus::ACTUAL, {1, 2});
        AddDocument(search_server, 3, std::string_view("funny pet and not very nasty rat"), DocumentStatus::ACTUAL, {1, 2});
        AddDocument(search_server, 4, std::string_view("pet with rat and rat and rat"), DocumentStatus::ACTUAL, {1, 2});
        AddDocument(search_server, 5, std::string_view("nasty rat with curly hair"), DocumentStatus::ACTUAL, {1, 2});

        //const string query = "curly and funny -not"s;
        const string_view query("curly and funny -not");
        //const char* query("curly and funny -not");

        {
            const auto [words, status] = search_server.MatchDocument(query, 1);
            cout << words.size() << " words for document 1"s << endl;
            // 1 words for document 1
        }
        {
            const auto [words, status] = search_server.MatchDocument(execution::seq, query, 1);
            cout << words.size() << " words for document 1 SEQ"s << endl;
            // 1 words for document 1
        }
        {
            const auto [words, status] = search_server.MatchDocument(execution::par, query, 1);
            cout << words.size() << " words for document 1 PAR"s << endl << endl;
            // 1 words for document 1
        }

        {
            const auto [words, status] = search_server.MatchDocument(query, 2);
            cout << words.size() << " words for document 2"s << endl;
            // 2 words for document 2
        }
        {
            const auto [words, status] = search_server.MatchDocument(execution::seq, query, 2);
            cout << words.size() << " words for document 2 SEQ"s << endl;
            // 2 words for document 2
        }
        {
            const auto [words, status] = search_server.MatchDocument(execution::par, query, 2);
            cout << words.size() << " words for document 2 PAR"s << endl << endl;
            // 2 words for document 2
        }

        {
            const auto [words, status] = search_server.MatchDocument(query, 3);
            cout << words.size() << " words for document 3"s << endl;
            // 0 words for document 3
        }
        {
            const auto [words, status] = search_server.MatchDocument(execution::seq, query, 3);
            cout << words.size() << " words for document 3 SEQ"s << endl;
            // 0 words for document 3
        }
        {
            const auto [words, status] = search_server.MatchDocument(execution::par, query, 3);
            cout << words.size() << " words for document 3 PAR"s << endl << endl;
            // 0 words for document 3
        }
    }

    mt19937 generator;

    const auto dictionary = GenerateDictionary(generator, 1000, 10);
    const auto documents = GenerateQueries(generator, dictionary, 10'000, 70);

    const string query = GenerateQuery(generator, dictionary, 500, 0.1);

    SearchServer search_server(dictionary[0]);
    for (size_t i = 0; i < documents.size(); ++i) {
        search_server.AddDocument(i, documents[i], DocumentStatus::ACTUAL, {1, 2, 3});
    }

    TEST(seq);
    TEST(par);

    //SearchServer search_server("and with"s);
/*
    AddDocument(search_server, 1, "funny pet and nasty rat"s, DocumentStatus::ACTUAL, {1, 2});
    AddDocument(search_server, 2, "funny pet with curly hair"s, DocumentStatus::ACTUAL, {1, 2});
    AddDocument(search_server, 3, "funny pet and not very nasty rat"s, DocumentStatus::ACTUAL, {1, 2});
    AddDocument(search_server, 4, "pet with rat and rat and rat"s, DocumentStatus::ACTUAL, {1, 2});
    AddDocument(search_server, 5, "nasty rat with curly hair"s, DocumentStatus::ACTUAL, {1, 2});
*/

/*

    const string query = "curly and funny"s;

    auto report = [&search_server, &query] {
        cout << search_server.GetDocumentCount() << " documents total, "s << endl;
    };

    //report();
    // однопоточная версия
    //search_server.RemoveDocument(5);
    //report();
    // однопоточная версия
    //search_server.RemoveDocument(execution::seq, 1);
    //report();
    // многопоточная версия
    //search_server.RemoveDocument(execution::par, 2);
    //report();

    mt19937 generator;

    const auto dictionary = GenerateDictionary(generator, 10'000, 25);
    const auto documents = GenerateQueries(generator, dictionary, 10'000, 100);

    {
        SearchServer search_server(dictionary[0]);
        for (size_t i = 0; i < documents.size(); ++i) {
            search_server.AddDocument(i, documents[i], DocumentStatus::ACTUAL, {1, 2, 3});
        }

        TEST(seq);
    }

    {
        SearchServer search_server(dictionary[0]);
        for (size_t i = 0; i < documents.size(); ++i) {
            search_server.AddDocument(i, documents[i], DocumentStatus::ACTUAL, {1, 2, 3});
        }

        TEST(par);
    }


    mt19937 generator;
    const auto dictionary = GenerateDictionary(generator, 2'000, 25);
    const auto documents = GenerateQueries(generator, dictionary, 20'000, 10);

    SearchServer search_server(dictionary[0]);
    for (size_t i = 0; i < documents.size(); ++i) {
        search_server.AddDocument(i, documents[i], DocumentStatus::ACTUAL, {1, 2, 3});
    }

    const auto queries = GenerateQueries(generator, dictionary, 2'000, 7);
    TEST(ProcessQueries);

    const vector<string> queries = {
        "nasty rat -not"s,
        "not very funny nasty pet"s,
        "curly hair"s
    };

    for (const Document& document : ProcessQueriesJoined(search_server, queries)) {
        cout << "Document "s << document.id << " matched with relevance "s << document.relevance << endl;
    }


    SearchServer search_server("and with"s);

    AddDocument(search_server, 1, "funny pet and nasty rat"s, DocumentStatus::ACTUAL, {7, 2, 7});
    AddDocument(search_server, 2, "funny pet with curly hair"s, DocumentStatus::ACTUAL, {1, 2});

    // дубликат документа 2, будет удалён
    AddDocument(search_server, 3, "funny pet with curly hair"s, DocumentStatus::ACTUAL, {1, 2});

    // отличие только в стоп-словах, считаем дубликатом
    AddDocument(search_server, 4, "funny pet and curly hair"s, DocumentStatus::ACTUAL, {1, 2});

    // множество слов такое же, считаем дубликатом документа 1
    AddDocument(search_server, 5, "funny funny pet and nasty nasty rat"s, DocumentStatus::ACTUAL, {1, 2});

    // добавились новые слова, дубликатом не является
    AddDocument(search_server, 6, "funny pet and not very nasty rat"s, DocumentStatus::ACTUAL, {1, 2});

    // множество слов такое же, как в id 6, несмотря на другой порядок, считаем дубликатом
    AddDocument(search_server, 7, "very nasty rat and not very funny pet"s, DocumentStatus::ACTUAL, {1, 2});

    // есть не все слова, не является дубликатом
    AddDocument(search_server, 8, "pet with rat and rat and rat"s, DocumentStatus::ACTUAL, {1, 2});

    // слова из разных документов, не является дубликатом
    AddDocument(search_server, 9, "nasty rat with curly hair"s, DocumentStatus::ACTUAL, {1, 2});

    cout << "Before duplicates removed: "s << search_server.GetDocumentCount() << endl;
    {
        LOG_DURATION("RemoveDuplicates");
        RemoveDuplicates(search_server);
    }
    cout << "After duplicates removed: "s << search_server.GetDocumentCount() << endl;

    SearchServer search_server("and in at"s);
    RequestQueue request_queue(search_server);

    search_server.AddDocument(1, "curly cat curly tail"s, DocumentStatus::ACTUAL, {7, 2, 7});
    search_server.AddDocument(2, "curly dog and fancy collar"s, DocumentStatus::ACTUAL, {1, 2, 3});
    search_server.AddDocument(3, "big cat fancy collar "s, DocumentStatus::ACTUAL, {1, 2, 8});
    search_server.AddDocument(4, "big dog sparrow Eugene"s, DocumentStatus::ACTUAL, {1, 3, 2});
    search_server.AddDocument(5, "big dog sparrow Vasiliy"s, DocumentStatus::ACTUAL, {1, 1, 1});

    {
        LOG_DURATION_STREAM("Long task", cout);
        MatchDocuments(search_server, "big dog"s);
    }

    {
        LOG_DURATION_STREAM("Long task", cout);
        FindTopDocuments(search_server, "big dog"s);
    }

    // 1439 запросов с нулевым результатом
    for (int i = 0; i < 1439; ++i) {
        request_queue.AddFindRequest("empty request"s);
    }
    // все еще 1439 запросов с нулевым результатом
    request_queue.AddFindRequest("curly dog"s);
    // новые сутки, первый запрос удален, 1438 запросов с нулевым результатом
    request_queue.AddFindRequest("big collar"s);
    // первый запрос удален, 1437 запросов с нулевым результатом
    request_queue.AddFindRequest("sparrow"s);
    cout << "Total empty requests: "s << request_queue.GetNoResultRequests() << endl;
*/
    return 0;
}
