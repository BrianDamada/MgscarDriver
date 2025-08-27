#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <json.hpp>
#include "serialCommunicator.h"
#include "password.h"

using json = nlohmann::json;

void clearScreen() {
    std::cout << "\033[2J\033[1;1H";
}

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
std::mutex uidMutex;
std::condition_variable uidCv;
std::string lastUID;
bool uidAvailable = false;

std::mutex connMutex;
std::condition_variable connCv;
bool arduinoConnected = false;
std::mutex handshakeMutex;
std::condition_variable handshakeCv;
bool handshakeSeen = false;

// ===================== REGISTRO DE USUÁRIOS =========================
json load_registry(const std::string& fname) {
    std::ifstream f(fname);
    if (!f) return json::object();
    json j;
    f >> j;
    return j;
}
void save_registry(const std::string& fname, json& j) {
    std::ofstream f(fname);
    f << j.dump(2);
}
json get_user_by_uuid(const json& registry, const std::string& uuid) {
    if (registry.contains(uuid)) return registry[uuid];
    return nullptr;
}
void register_user(const std::string& fname, const std::string& uuid, const std::string& name) {
    json registry = load_registry(fname);
    registry[uuid]["nome"] = name;
    // Adiciona dicionário de sites se não existir
    if (!registry[uuid].contains("sites")) registry[uuid]["sites"] = json::object();
    save_registry(fname, registry);
}
void add_site_to_user(const std::string& fname, const std::string& uuid, const std::string& site, const std::string& email, const std::string& username) {
    json registry = load_registry(fname);
    if (!registry.contains(uuid)) return;
    registry[uuid]["sites"][site] = {
        {"email", email},
        {"username", username}
    };
    save_registry(fname, registry);
}
json get_sites_by_uuid(const json& registry, const std::string& uuid) {
    if (registry.contains(uuid) && registry[uuid].contains("sites")) return registry[uuid]["sites"];
    return json::object();
}
// ====================================================================

void lcd_show_welcome(SerialManager& sm, const std::string& name) {
    sm.writeLine("LCD1:Bem vindo!");
    std::string msg = name;
    if (msg.size() > 16) msg = msg.substr(0, 16);
    sm.writeLine("LCD2:" + msg);
}

std::string wait_for_uid(int timeout_sec=30) {
    std::cout << "Aproxime o cartão ao Arduino agora (timeout " << timeout_sec << "s)..." << std::endl;
    std::unique_lock<std::mutex> lock(uidMutex);
    uidAvailable = false;
    lastUID.clear();
    bool got = uidCv.wait_for(lock, std::chrono::seconds(timeout_sec), []{ return uidAvailable; });
    if (!got) return "";
    return lastUID;
}

#if defined(_WIN32)
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
    #endif
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
#endif

int main() {
    #if defined(_WIN32)
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    #endif

    
    try {
        std::cout << "Procurando Arduino MGSCAR (handshake)..." << std::endl;
        std::string port = SerialManager::findMgscar("MGSCAR-100", true);
        SerialManager sm;
        sm.open(port);

        sm.startReader([&](const std::string& line){
            {
                std::lock_guard<std::mutex> lock(coutMutex);
                std::cout << "[ARDUINO] " << line << std::endl;
            }
            if (line == "MGSCAR-100") {
                { std::lock_guard<std::mutex> lock(handshakeMutex); handshakeSeen = true; }
                handshakeCv.notify_one();
            }
            if (line == "CONNECTED") {
                { std::lock_guard<std::mutex> lock2(connMutex); arduinoConnected = true; }
                connCv.notify_one();
            }
            if (is_hex_string(line)) {
                std::lock_guard<std::mutex> lock2(uidMutex);
                lastUID = line;
                uidAvailable = true;
                uidCv.notify_one();
            }
        });

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
        try {
            sm.writeLine("300");
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

        // ========== REGISTRO E LOGIN AUTOMÁTICO ==========
        const std::string registryFile = "user_registry.json";
        std::cout << "\nInício: aproxime seu cartão para identificação...\n";
        std::string uuid = wait_for_uid(60);
        if (uuid.empty()) {
            std::cout << "Timeout: não foi recebido UID do cartão. Encerrando." << std::endl;
            sm.stop(); sm.close(); return 1;
        }
        json registry = load_registry(registryFile);
        json user = get_user_by_uuid(registry, uuid);
        if (!user.is_null()) {
            std::string name = user.value("nome", "Usuário");
            lcd_show_welcome(sm, name);
            std::cout << "Bem vindo, " << name << "!\n";
        } else {
            clearScreen();
            std::cout << "Cartão não cadastrado. Iniciando cadastro...\n";
            std::string name;
            std::cout << "Nome: "; std::getline(std::cin, name); trim_inplace(name);
            register_user(registryFile, uuid, name);
            lcd_show_welcome(sm, name);
            std::cout << "Cadastro realizado. Bem vindo, " << name << "!\n";
        }

        // ================== COMANDOS ===================
        std::cout << "\nComandos: cadastrarSenha(), recuperarSenha(), writeLine1(\"texto\"), writeLine2(\"texto\"), exit\n";
        std::string input;
        while (true) {
            std::cout << "> ";
            if (!std::getline(std::cin, input)) break;
            trim_inplace(input);
            if (input.empty()) continue;
            try {
                if (input.rfind("writeLine1", 0) == 0) {
                    clearScreen();
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
                    clearScreen();
                    auto start = input.find('(');
                    auto end = input.rfind(')');
                    if (start!=std::string::npos && end!=std::string::npos && end>start+1) {
                        std::string inside = input.substr(start+1, end-start-1);
                        trim_inplace(inside);
                        if ((inside.front()=='\"' && inside.back()=='\"') || (inside.front()=='\'' && inside.back()=='\'')) {
                            inside = inside.substr(1, inside.size()-2);
                        }
                        if (inside.size() > 16) inside = inside.substr(0,16);
                        sm.writeLine(std::string("LCD2:" + inside));
                    } else {
                        std::cout << "Uso: writeLine2(\"texto\")" << std::endl;
                    }
                } else if (input == "cadastrarSenha()" || input == "cadastrarSenha") {
                    // Cadastro de senha para um site
                    clearScreen();
                    std::string site, email, username, masterPassword;
                    int length = 12;
                    std::cout << "Site: "; std::getline(std::cin, site); trim_inplace(site);
                    std::cout << "Email: "; std::getline(std::cin, email); trim_inplace(email);
                    std::cout << "Username: "; std::getline(std::cin, username); trim_inplace(username);
                    std::cout << "Tamanho da senha (ex: 12): ";
                    std::string line;
                    std::getline(std::cin, line); trim_inplace(line);
                    if (!line.empty()) length = std::stoi(line);
                    std::cout << "Incluir maiúsculas? (y/n): "; std::getline(std::cin, line); trim_inplace(line);
                    bool u = (line.size() > 0 && (line[0]=='y' || line[0]=='Y'));
                    std::cout << "Incluir minúsculas? (y/n): "; std::getline(std::cin, line); trim_inplace(line);
                    bool l = (line.size() > 0 && (line[0]=='y' || line[0]=='Y'));
                    std::cout << "Incluir números? (y/n): "; std::getline(std::cin, line); trim_inplace(line);
                    bool n = (line.size() > 0 && (line[0]=='y' || line[0]=='Y'));
                    std::cout << "Incluir caracteres especiais? (y/n): "; std::getline(std::cin, line); trim_inplace(line);
                    bool c = (line.size() > 0 && (line[0]=='y' || line[0]=='Y'));
                    std::cout << "Digite a masterPassword: "; std::getline(std::cin, masterPassword); trim_inplace(masterPassword);
                    std::string seed = uuid + masterPassword + site + email + username;
                    std::string senha = generatePassword(seed, length, u, l, n, c);
                    std::cout << "\nSenha gerada para " << site << ": " << senha << std::endl;
                    sm.writeLine(std::string("LCD1:") + (senha.size()>16 ? senha.substr(0,16) : senha));
                    add_site_to_user(registryFile, uuid, site, email, username);
                } else if (input == "recuperarSenha()" || input == "recuperarSenha") {
                    clearScreen();
                    std::cout << "Aproxime o cartão para recuperar senha (timeout 30s)...\n";
                    std::string uid = wait_for_uid(30);
                    if (uid.empty()) {
                        std::cout << "Timeout: não foi recebido UID do cartão." << std::endl;
                        continue;
                    }
                    json registry = load_registry(registryFile);
                    json user = get_user_by_uuid(registry, uid);
                    if (user.is_null()) {
                        std::cout << "Cartão não cadastrado!\n";
                        continue;
                    }
                    json sites = get_sites_by_uuid(registry, uid);
                    if (sites.empty()) {
                        std::cout << "Nenhum site cadastrado nesse cartão!\n";
                        continue;
                    }
                    std::cout << "\n\nSites cadastrados:\n";
                    for (auto it = sites.begin(); it != sites.end(); ++it) {
                        std::cout << "- " << it.key() << std::endl;
                    }
                    std::string site;
                    std::cout << "Escolha o site: "; std::getline(std::cin, site); trim_inplace(site);
                    if (!sites.contains(site)) {
                        std::cout << "Site não encontrado!\n";
                        continue;
                    }
                    std::string masterPassword;
                    std::cout << "Digite a masterPassword: "; std::getline(std::cin, masterPassword); trim_inplace(masterPassword);
                    int length = 12;
                    bool u=true, l=true, n=true, c=true;
                    // Pega dados do registro
                    std::string email = sites[site].value("email", "");
                    std::string username = sites[site].value("username", "");
                    std::string seed = uid + masterPassword + site + email + username;
                    std::string senha = generatePassword(seed, length, u, l, n, c);
                    std::cout << "\nSenha recuperada para " << site << ": " << senha << std::endl;
                    sm.writeLine(std::string("LCD1:") + (senha.size()>16 ? senha.substr(0,16) : senha));
                } else if (input == "exit") {
                    std::cout << "Encerrando..." << std::endl;
                    break;
                } else {
                    std::cout << "Comando desconhecido. Comandos suportados: cadastrarSenha(), recuperarSenha(), writeLine1(\"texto\"), writeLine2(\"texto\"), exit" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Erro: " << e.what() << std::endl;
            }
        }
        sm.stop(); sm.close();
        std::cout << "Encerrado." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Erro fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}