#include "remove_duplicates.h"

void RemoveDuplicates(SearchServer& search_server) {
    std::set<std::string> uniq_words;
    std::set<int> del_ids;
    for (int id : search_server) {
        std::string w = "";
        for(auto& m : search_server.GetWordFrequencies(id)) {
            w += m.first;
        }
        if(uniq_words.find(w) == uniq_words.end()) {
            uniq_words.insert(w);
        } else {
            del_ids.insert(id);
        }
    }

    for(auto id : del_ids) {
        search_server.RemoveDocument(id);
        std::cout << "Found duplicate document id " << id << std::endl;
    }
}
