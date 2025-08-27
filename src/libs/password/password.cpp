#include "password.h"

string charactersUsed(bool u, bool l, bool n, bool c){
    if (!u && !l && !n && !c){
        throw runtime_error("ERRO: é preciso que pelo menos um esteja ativo");
    }

    vector<UseType> types = {
        {u, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
        {l, "abcdefghijklmnopqrstuvwxyz"},
        {n, "0123456789"},
        {c, ",.;:|/?{}[]`'^~!@#$%&*_-+=<>'\""},
    };

    string result;
    for (const auto& type : types) {
        result+=type.get();
    }

    return result;
}


string generatePassword(std::string seed, int length, bool u, bool l, bool n, bool c){
    hash<string> hasher;
    size_t Intseed = hasher(seed);

    string characters_used = charactersUsed(u, l, n, c);
    std::mt19937 rng(Intseed);

    string result;
    for (int i = 0; i < length; i++){
        result+=characters_used[rng() % characters_used.size()];
    }

    return result;
}