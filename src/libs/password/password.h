#include <cstddef>
#include <functional>
#include <random>
#include <string>
#include <vector>
#include <stdexcept>


using std::string;
using std::vector;
using std::hash;
using std::runtime_error;

struct UseType{
    bool active;
    string text;

    string get() const{
        return active ? text : "";
    }
};

// função para definir quais os caracteres serão usados quais não
// retorna uma string com os caracteres definidos como true, como false não retorna nada
std::string charactersUsed(bool u, bool l, bool n, bool c);



// função de geração da senha em questão, passa-se a 'seed' que pode ser qualquer coisa, passa-se o tamanho então passa-se as configurações
// que é o mesmo caso do anterior true para habilitar um tipo de caractere e false para desabilitar

// tabela:
// u            Uppercase               Letras Maiusculas
// l            Lowwercase              Letras Minusculas
// u            Numbers                 Numeros
// c            Characters              Caracteres Especiais
string generatePassword(std::string seed, int length, bool u, bool l, bool n, bool c);