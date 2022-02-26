#pragma once

#include <string>
#include <vector>
#include <deque>

#include "search_server.h"

class RequestQueue {
public:
    explicit RequestQueue(const SearchServer& search_server);
    // сделаем "обёртки" для всех методов поиска, чтобы сохранять результаты для нашей статистики
    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentPredicate document_predicate);

    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentStatus status);

    std::vector<Document> AddFindRequest(const std::string& raw_query);

    int GetNoResultRequests() const;
private:
    struct QueryResult {
        bool nores = true;
        int count_res = 0;
        int count_nores = 0;
    };
    const SearchServer& search_server_;
    std::deque<QueryResult> requests_;
    const static int min_in_day_ = 1440;

    void PushFindRequest(bool nores);
};
