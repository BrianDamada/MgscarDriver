// serialCommunicator.h
#ifndef SERIAL_COMMUNICATOR_H
#define SERIAL_COMMUNICATOR_H

#include <string>
#include <functional>
#include <thread>
#include <memory>

namespace serial {
    class Serial;
}

class SerialManager {
public:
    using OnReceiveCallback = std::function<void(const std::string&)>;

    SerialManager();
    ~SerialManager();

    // Procura o MGSCAR (envia 300 ao encontrar)
    // Pode lançar runtime_error se não encontrar.
    static std::string findMgscar(const std::string& searchKey, bool returnActivated);

    // Abre porta (já em 9600)
    void open(const std::string& port);

    // Fecha
    void close();

    // Envia linha (adiciona newline)
    void writeLine(const std::string& payload);

    // Inicia thread de leitura; callback chama-se para cada linha recebida
    void startReader(OnReceiveCallback cb);

    // Para a thread de leitura e fecha
    void stop();

    // verifica se a serial está aberta
    bool isOpen() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // SERIAL_COMMUNICATOR_H
