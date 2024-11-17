#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <iomanip>
#include "pci.h" // Подключение файла с таблицей

namespace fs = std::filesystem;

// Функция для поиска производителя по Vendor ID
const char* find_vendor_name(unsigned short vendor_id) {
    for (const auto& entry : PciVenTable) {
        if (entry.VendorId == vendor_id) {
            return entry.VendorName;
        }
    }
    return "Unknown Vendor";
}

void list_pci_devices() {
    const std::string pci_path = "/sys/bus/pci/devices/";

    if (!fs::exists(pci_path)) {
        std::cerr << "PCI path does not exist. Ensure you have access to /sys/bus/pci/devices/" << std::endl;
        return;
    }

    for (const auto& entry : fs::directory_iterator(pci_path)) {
        if (entry.is_directory()) {
            std::string device_path = entry.path();
            std::string vendor_file = device_path + "/vendor";
            std::string device_file = device_path + "/device";
            std::string header_type_file = device_path + "/header_type";

            // Проверка существования файлов vendor и device
            if (!fs::exists(vendor_file) || !fs::exists(device_file)) {
                std::cerr << "Missing vendor or device file for " << device_path << std::endl;
                continue;
            }

            std::ifstream vendor_stream(vendor_file);
            std::ifstream device_stream(device_file);

            if (!vendor_stream || !device_stream) {
                std::cerr << "Failed to open vendor or device file for " << device_path << std::endl;
                continue;
            }

            // Считывание информации о vendor и device
            std::string vendor_id_str, device_id_str;
            std::getline(vendor_stream, vendor_id_str);
            std::getline(device_stream, device_id_str);

            if (vendor_id_str.empty() || device_id_str.empty()) {
                std::cerr << "Failed to read valid vendor/device info for " << device_path << std::endl;
                continue;
            }

            unsigned short vendor_id = std::stoi(vendor_id_str, nullptr, 16);
            unsigned short device_id = std::stoi(device_id_str, nullptr, 16);
            const char* vendor_name = find_vendor_name(vendor_id);

            // Извлекаем адрес устройства из пути
            std::string device_address = entry.path().filename();

            // Форматированный вывод основной информации
            std::cout << "Address: " << device_address << "\n"
                      << "  Vendor ID: 0x" << std::hex << std::setw(4) << std::setfill('0') << vendor_id << "\n"
                      << "  Device ID: 0x" << std::hex << std::setw(4) << std::setfill('0') << device_id << "\n"
                      << "  Manufacturer: " << vendor_name << "\n";

            // Проверка существования header_type
            unsigned char header_type = 0; // По умолчанию предположим, что header type равен 0 (не мост)
            if (fs::exists(header_type_file)) {
                std::ifstream header_type_stream(header_type_file);
                if (header_type_stream) {
                    std::string header_type_str;
                    std::getline(header_type_stream, header_type_str);
                    if (!header_type_str.empty()) {
                        header_type = std::stoi(header_type_str, nullptr, 16);
                    }
                }
            }

            // Проверка на тип заголовка - если это не мост (Header Type == 0)
            if ((header_type & 0x7F) == 0) {
                std::cout << "  Header Type: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)header_type << " (Non-Bridge Device)" << "\n";

                // Чтение и вывод значений базовых регистров памяти (BAR)
                for (int bar_index = 0; bar_index < 6; ++bar_index) {
                    std::string bar_file = device_path + "/resource" + std::to_string(bar_index);
                    if (fs::exists(bar_file)) {
                        std::ifstream bar_stream(bar_file);
                        if (bar_stream) {
                            std::string bar_value_str;
                            std::getline(bar_stream, bar_value_str);

                            if (!bar_value_str.empty()) {
                                unsigned long long bar_value = std::stoull(bar_value_str, nullptr, 16);
                                
                                // Диагностический вывод для BAR
                                std::cout << "  Reading BAR" << bar_index << ": " << bar_value_str << " (parsed as 0x" << std::hex << bar_value << ")\n";

                                if (bar_value != 0) {
                                    // Расшифровка значения BAR
                                    std::cout << "  BAR" << bar_index << ": 0x" << std::hex << bar_value << "\n";
                                    if (bar_value & 1) {
                                        std::cout << "    Type: I/O Space\n"
                                                  << "    Address: 0x" << (bar_value & ~0x3) << "\n";
                                    } else {
                                        std::cout << "    Type: Memory Space\n";
                                        if (bar_value & 0x8) {
                                            std::cout << "    Prefetchable: Yes\n";
                                        } else {
                                            std::cout << "    Prefetchable: No\n";
                                        }
                                        std::cout << "    Address: 0x" << (bar_value & ~0xF) << "\n";
                                    }
                                } else {
                                    std::cout << "  BAR" << bar_index << " value is 0 (no resource allocated)\n";
                                }
                            }
                        } else {
                            std::cerr << "  Failed to read BAR" << bar_index << " for " << device_path << std::endl;
                        }
                    } else {
                        std::cerr << "  BAR file resource" << bar_index << " does not exist for " << device_path << std::endl;
                    }
                }

                // Чтение и вывод значения Interrupt Line
                std::string irq_file = device_path + "/irq";
                if (fs::exists(irq_file)) {
                    std::ifstream irq_stream(irq_file);
                    if (irq_stream) {
                        std::string irq_value_str;
                        std::getline(irq_stream, irq_value_str);

                        if (!irq_value_str.empty()) {
                            int irq_value = std::stoi(irq_value_str);
                            std::cout << "  Interrupt Line: " << irq_value << "\n";
                            if (irq_value == 0) {
                                std::cout << "    Interrupt not assigned or disabled.\n";
                            } else {
                                std::cout << "    Interrupt assigned to line: " << irq_value << "\n";
                            }
                        }
                    } else {
                        std::cerr << "  Failed to read IRQ info for " << device_path << std::endl;
                    }
                } else {
                    std::cerr << "  IRQ file does not exist for " << device_path << std::endl;
                }

                // Чтение и вывод значения Interrupt Pin
                std::string config_file = device_path + "/config";
                if (fs::exists(config_file)) {
                    std::ifstream config_stream(config_file, std::ios::binary);
                    if (config_stream) {
                        config_stream.seekg(0x3D); // Смещение для поля Interrupt Pin
                        unsigned char interrupt_pin = 0;
                        config_stream.read(reinterpret_cast<char*>(&interrupt_pin), 1);

                        if (interrupt_pin > 0 && interrupt_pin <= 4) {
                            char pin_letter = 'A' + (interrupt_pin - 1);
                            std::cout << "  Interrupt Pin: INTA#" << pin_letter << "\n";
                        } else {
                            std::cout << "  Interrupt Pin: Not used or invalid value\n";
                        }
                    } else {
                        std::cerr << "  Failed to read Interrupt Pin from config file for " << device_path << std::endl;
                    }
                } else {
                    std::cerr << "  Config file does not exist for " << device_path << std::endl;
                }

            } else {
                std::cout << "  Header Type: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)header_type << " (Bridge Device)" << "\n";
            }

            std::cout << "\n";
        }
    }
}

int main() {
    list_pci_devices();
    return 0;
}
