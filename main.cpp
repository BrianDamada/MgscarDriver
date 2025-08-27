// main.cpp (atualizado: espera MGSCAR-100 antes de enviar 300)
#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>

#include "serialCommunicator.h"
#include "password.h" // seu arquivo de password (implementação em password.cpp)

static inline void trim_inplace(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
}

static inline bool is_hex_string(const std::string &s) {
    if (s.empty()) return false;
    int len = (int)s.size();
    if (len < 4 || len > 64) return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::mutex coutMutex;

// Variables to receive UID from reader callback
std::mutex uidMutex;
std::condition_variable uidCv;
std::string lastUID;
bool uidAvailable = false;

// handshake/connection flags
std::mutex connMutex;
std::condition_variable connCv;
bool arduinoConnected = false;

std::mutex handshakeMutex;
std::condition_variable handshakeCv;
bool handshakeSeen = false;

int main() {
    try {
        std::cout << "Procurando Arduino MGSCAR (handshake)..." << std::endl;
        std::string port = SerialManager::findMgscar("MGSCAR-100", true);

        SerialManager sm;
        sm.open(port);

        // callback que recebe linhas do Arduino
        sm.startReader([&](const std::string& line){
            {
                std::lock_guard<std::mutex> lock(coutMutex);
                std::cout << "[ARDUINO] " << line << std::endl;
            }

            // se receber MGSCAR-100 sabemos que o Arduino está em handshake
            if (line == "MGSCAR-100") {
                {
                    std::lock_guard<std::mutex> lock(handshakeMutex);
                    handshakeSeen = true;
                }
                handshakeCv.notify_one();
            }

            // se Arduino informou conexão, notifica
            if (line == "CONNECTED") {
                {
                    std::lock_guard<std::mutex> lock2(connMutex);
                    arduinoConnected = true;
                }
                connCv.notify_one();
            }

            // se for hex UID, notifica thread de geração de senha
            if (is_hex_string(line)) {
                std::lock_guard<std::mutex> lock2(uidMutex);
                lastUID = line;
                uidAvailable = true;
                uidCv.notify_one();
            }
        });

        // Esperar MGSCAR-100 (até 5s) para garantir que Arduino completou boot/handshake
        {
            std::unique_lock<std::mutex> hlock(handshakeMutex);
            bool got = handshakeCv.wait_for(hlock, std::chrono::seconds(5), []{ return handshakeSeen; });
            if (!got) {
                std::cout << "Aviso: não recebeu MGSCAR-100 logo após abrir porta; aguardando 2s de boot antes de enviar 300..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(2));
            } else {
                std::cout << "Recebido MGSCAR-100 do Arduino; enviando 300 agora." << std::endl;
            }
        }

        // envia 300 (ponto de parada) e espera CONNECTED (até 5s)
        try {
            sm.writeLine("300"); // envia "300\n"
            {
                std::unique_lock<std::mutex> lock(connMutex);
                bool got = connCv.wait_for(lock, std::chrono::seconds(5), []{ return arduinoConnected; });
                if (got) {
                    std::cout << "Handshake confirmado pelo Arduino (CONNECTED)." << std::endl;
                } else {
                    std::cout << "Aviso: não foi recebida confirmação CONNECTED em 5s. Continuando mesmo assim." << std::endl;
                }
            }
        } catch (const std::exception &e) {
            std::cout << "Erro ao enviar 300: " << e.what() << std::endl;
        }

        std::cout << "Terminal conectado. Comandos: writeLine1(\"texto\"), writeLine2(\"texto\"), generatePassword(), exit" << std::endl;

        std::string input;
        while (true) {
            std::cout << "> ";
            if (!std::getline(std::cin, input)) break;
            trim_inplace(input);
            if (input.empty()) continue;

            try {
                if (input.rfind("writeLine1", 0) == 0) {
                    auto start = input.find('(');
                    auto end = input.rfind(')');
                    if (start!=std::string::npos && end!=std::string::npos && end>start+1) {
                        std::string inside = input.substr(start+1, end-start-1);
                        trim_inplace(inside);
                        if ((inside.front()=='\"' && inside.back()=='\"') || (inside.front()=='\'' && inside.back()=='\'')) {
                            inside = inside.substr(1, inside.size()-2);
                        }
                        if (inside.size() > 16) inside = inside.substr(0,16);
                        sm.writeLine(std::string("LCD1:") + inside);
                    } else {
                        std::cout << "Uso: writeLine1(\"texto\")" << std::endl;
                    }
                } else if (input.rfind("writeLine2", 0) == 0) {
                    auto start = input.find('(');
                    auto end = input.rfind(')');
                    if (start!=std::string::npos && end!=std::string::npos && end>start+1) {
                        std::string inside = input.substr(start+1, end-start-1);
                        trim_inplace(inside);
                        if ((inside.front()=='\"' && inside.back()=='\"') || (inside.front()=='\'' && inside.back()=='\'')) {
                            inside = inside.substr(1, inside.size()-2);
                        }
                        if (inside.size() > 16) inside = inside.substr(0,16);
                        sm.writeLine(std::string("LCD2:") + inside);
                    } else {
                        std::cout << "Uso: writeLine2(\"texto\")" << std::endl;
                    }
                } else if (input == "generatePassword()" || input == "generatePassword") {
                    int length = 12;
                    std::string line;
                    std::cout << "Tamanho da senha (ex: 12): ";
                    std::getline(std::cin, line);
                    trim_inplace(line);
                    if (!line.empty()) length = std::stoi(line);

                    std::cout << "Incluir maiúsculas? (y/n): ";
                    std::getline(std::cin, line); trim_inplace(line);
                    bool u = (line.size() > 0 && (line[0]=='y' || line[0]=='Y'));

                    std::cout << "Incluir minúsculas? (y/n): ";
                    std::getline(std::cin, line); trim_inplace(line);
                    bool l = (line.size() > 0 && (line[0]=='y' || line[0]=='Y'));

                    std::cout << "Incluir números? (y/n): ";
                    std::getline(std::cin, line); trim_inplace(line);
                    bool n = (line.size() > 0 && (line[0]=='y' || line[0]=='Y'));

                    std::cout << "Incluir caracteres especiais? (y/n): ";
                    std::getline(std::cin, line); trim_inplace(line);
                    bool c = (line.size() > 0 && (line[0]=='y' || line[0]=='Y'));

                    std::cout << "Aproxime o cartão ao Arduino agora (timeout 30s)..." << std::endl;

                    // espera UID
                    {
                        std::unique_lock<std::mutex> lock(uidMutex);
                        uidAvailable = false;
                        lastUID.clear();
                        bool got = uidCv.wait_for(lock, std::chrono::seconds(30), []{ return uidAvailable; });
                        if (!got) {
                            std::cout << "Timeout: não foi recebido UID do cartão." << std::endl;
                        } else {
                            std::string seed = lastUID;
                            std::cout << "UID recebido: " << seed << std::endl;
                            std::string senha = generatePassword(seed, length, u, l, n, c);
                            std::cout << "Senha gerada: " << senha << std::endl;
                            sm.writeLine(std::string("LCD1:") + (senha.size()>16 ? senha.substr(0,16) : senha));
                        }
                    }
                } else if (input == "exit") {
                    std::cout << "Encerrando..." << std::endl;
                    break;
                } else {
                    std::cout << "Comando desconhecido. Comandos suportados: writeLine1(\"texto\"), writeLine2(\"texto\"), generatePassword(), exit" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Erro: " << e.what() << std::endl;
            }
        }

        sm.stop();
        sm.close();
        std::cout << "Encerrado." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Erro fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
