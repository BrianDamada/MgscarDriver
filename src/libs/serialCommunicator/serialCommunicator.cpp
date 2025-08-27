// serialCommunicator.cpp
#include "serialCommunicator.h"

#include <serial/serial.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <stdexcept>

using std::string;

struct SerialManager::Impl {
    serial::Serial* serial = nullptr;
    std::thread readerThread;
    bool readerRunning = false;
    std::mutex mtx;
    SerialManager::OnReceiveCallback cb;

    ~Impl() {
        if (serial) {
            if (serial->isOpen()) serial->close();
            delete serial;
            serial = nullptr;
        }
    }
};

static inline void trim_inplace(string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
}

SerialManager::SerialManager() : impl_(new Impl()) {}
SerialManager::~SerialManager() {
    stop();
    impl_.reset();
}

std::string SerialManager::findMgscar(const std::string& searchKey, bool returnActivated) {
    auto ports = serial::list_ports();
    for (const auto& p : ports) {
        try {
            serial::Serial testSerial(p.port, 9600, serial::Timeout::simpleTimeout(1000));
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            string response = testSerial.readline(65536, "\n");
            trim_inplace(response);
            if (response.find(searchKey) != string::npos) {
                if (returnActivated) {
                    std::cout << "Arduino encontrado na porta: " << p.port << std::endl;
                }
                // responde com 300 para ativar handshake
                testSerial.write("300\n");
                return p.port;
            }
        } catch (const std::exception& e) {
            // ignorar
            continue;
        }
    }
    throw std::runtime_error("ERRO: Nenhum dispositivo MGSCAR encontrado.");
}

void SerialManager::open(const std::string& port) {
    if (impl_->serial && impl_->serial->isOpen()) return;
    impl_->serial = new serial::Serial(port, 9600, serial::Timeout::simpleTimeout(1000));
    if (!impl_->serial->isOpen()) {
        delete impl_->serial;
        impl_->serial = nullptr;
        throw std::runtime_error("Não foi possível abrir a porta serial: " + port);
    }
}

void SerialManager::close() {
    stop();
    if (impl_->serial) {
        if (impl_->serial->isOpen()) impl_->serial->close();
        delete impl_->serial;
        impl_->serial = nullptr;
    }
}

void SerialManager::writeLine(const std::string& payload) {
    if (!impl_->serial || !impl_->serial->isOpen()) {
        throw std::runtime_error("Serial não está aberta.");
    }
    impl_->serial->write(payload + "\n");
}

void SerialManager::startReader(OnReceiveCallback cb) {
    if (!impl_->serial || !impl_->serial->isOpen()) {
        throw std::runtime_error("Serial não está aberta para iniciar reader.");
    }
    if (impl_->readerRunning) return;
    impl_->cb = cb;
    impl_->readerRunning = true;
    impl_->readerThread = std::thread([this]() {
        try {
            while (impl_->readerRunning && impl_->serial && impl_->serial->isOpen()) {
                string line = impl_->serial->readline(65536, "\n");
                trim_inplace(line);
                if (!line.empty() && impl_->cb) {
                    impl_->cb(line);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        } catch (const std::exception& e) {
            // se der erro, envia uma linha com prefixo de erro
            if (impl_->cb) impl_->cb(string("ERROR:") + e.what());
        }
    });
}

void SerialManager::stop() {
    if (impl_->readerRunning) {
        impl_->readerRunning = false;
        if (impl_->readerThread.joinable()) impl_->readerThread.join();
    }
}

bool SerialManager::isOpen() const {
    return impl_->serial && impl_->serial->isOpen();
}
