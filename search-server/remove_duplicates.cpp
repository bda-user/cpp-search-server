#include "remove_duplicates.h"

void RemoveDuplicates(SearchServer& search_server) {
    std::set<std::set<std::string>> unique_words;
    std::set<int> del_docs;
    for (int doc_id : search_server) {
        //make document words sequencies
        std::set<std::string> doc_words;
        for(auto& [word, _] : search_server.GetWordFrequencies(doc_id)) {
            doc_words.insert(word);
        }
        if(unique_words.find(doc_words) == unique_words.end()) {
            //find new unique words sequencies
            unique_words.insert(doc_words);
        } else {
            //find duplicate document
            del_docs.insert(doc_id);
        }
    }

    for(auto doc_id : del_docs) {
        search_server.RemoveDocument(doc_id);
        std::cout << "Found duplicate document id " << doc_id << std::endl;
    }
}
