#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <ctime>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/syslog_relay.h>
#include <unistd.h>

std::string log_cache_location = "/Users/jack/Downloads/idevicelogger/logs";
unsigned long max_buffer_size = 512;

struct logger_connection {
    idevice_t device;
    lockdownd_client_t lockdown;
    syslog_relay_client_t syslog;
    std::string name;
    std::ofstream output;
    unsigned long bufferSize;
};

std::unordered_map<std::string, struct logger_connection*> connections;

void syslog_getCharacter(char c, void *user_data) {
    struct logger_connection * con = (struct logger_connection*)user_data;
    con->output.put(c);
    if (++con->bufferSize >= max_buffer_size) {
        con->output.flush();
        con->bufferSize = 0;
    }
}

int connectToDevice(std::string udid) {
    if (connections.find(udid) != connections.end()) return 0;
    struct logger_connection * con = new struct logger_connection;
    // Connect to device
    idevice_error_t idv_err = idevice_new(&con->device, udid.c_str());
    if (idv_err != IDEVICE_E_SUCCESS) {
        delete con; 
        return idv_err;
    }
    // Set up lockdownd connection
    lockdownd_error_t ldd_err = lockdownd_client_new_with_handshake(con->device, &con->lockdown, "idevicelogger");
    while (ldd_err == LOCKDOWN_E_PAIRING_DIALOG_RESPONSE_PENDING || ldd_err == LOCKDOWN_E_PASSWORD_PROTECTED) {
        usleep(1000000);
        ldd_err = lockdownd_client_new_with_handshake(con->device, &con->lockdown, "idevicelogger");
    }
    if (ldd_err != LOCKDOWN_E_SUCCESS) {
        idevice_free(con->device); 
        delete con; 
        return ldd_err;
    }
    // Pair with device
    // ldd_err = lockdownd_validate_pair(con->lockdown, NULL);
    // if (ldd_err != LOCKDOWN_E_SUCCESS) {
    //     ldd_err = lockdownd_pair(con->lockdown, NULL);
    //     while (ldd_err == LOCKDOWN_E_PAIRING_DIALOG_RESPONSE_PENDING) {
    //         usleep(1000000);
    //         ldd_err = lockdownd_pair(con->lockdown, NULL);
    //     }
    // }
    // if (ldd_err != LOCKDOWN_E_SUCCESS) {
    //     lockdownd_client_free(con->lockdown); 
    //     idevice_free(con->device); 
    //     delete con; 
    //     return ldd_err;
    // }
    // lockdownd_client_free(con->lockdown);
    // ldd_err = lockdownd_client_new_with_handshake(con->device, &con->lockdown, "idevicelogger");
    // if (ldd_err != LOCKDOWN_E_SUCCESS) {
    //     idevice_free(con->device); 
    //     delete con; 
    //     return ldd_err;
    // }
    // Get device name
    char * name = NULL;
    ldd_err = lockdownd_get_device_name(con->lockdown, &name);
    if (ldd_err != LOCKDOWN_E_SUCCESS) {
        lockdownd_client_free(con->lockdown); 
        idevice_free(con->device); 
        delete con; 
        return ldd_err;
    }
    if (name != NULL) {
        con->name = std::string(name);
        free(name);
    }
    // Start syslog service
    lockdownd_service_descriptor_t srv = NULL;
    ldd_err = lockdownd_start_service(con->lockdown, SYSLOG_RELAY_SERVICE_NAME, &srv);
    if (ldd_err != LOCKDOWN_E_SUCCESS) {
        lockdownd_client_free(con->lockdown); 
        idevice_free(con->device); 
        delete con; 
        return ldd_err;
    }
    syslog_relay_error_t slr_err = syslog_relay_client_new(con->device, srv, &con->syslog);
    if (slr_err != SYSLOG_RELAY_E_SUCCESS) {
        lockdownd_client_free(con->lockdown); 
        idevice_free(con->device); 
        delete con; 
        return slr_err;
    }
    // Open log file
    mkdir((log_cache_location + "/" + con->name).c_str(), 0777);
    char * datestr = new char[20];
    time_t now = time(0);
    struct tm * myTime = localtime(&now);
    strftime(datestr, 20, "%F_%H%M%S", myTime);
    con->output.open(log_cache_location + "/" + con->name + "/" + std::string(datestr) + ".log");
    delete[] datestr;
    if (!con->output.is_open()) {
        syslog_relay_client_free(con->syslog); 
        lockdownd_client_free(con->lockdown); 
        idevice_free(con->device); 
        delete con; 
        return -1029;
    }
    con->bufferSize = 0;
    // Start logging
    slr_err = syslog_relay_start_capture(con->syslog, syslog_getCharacter, con);
    if (slr_err != SYSLOG_RELAY_E_SUCCESS) {
        con->output.close(); 
        syslog_relay_client_free(con->syslog); 
        lockdownd_client_free(con->lockdown); 
        idevice_free(con->device); 
        delete con; 
        return slr_err;
    }
    // Store new connection for later disconnect
    connections[udid] = con;
    return 0;
}

int disconnectFromDevice(std::string udid) {
    if (connections.find(udid) == connections.end()) return 0;
    struct logger_connection * con = connections[udid];
    // Stop capturing
    syslog_relay_error_t slr_err = syslog_relay_stop_capture(con->syslog);
    if (slr_err != SYSLOG_RELAY_E_SUCCESS) return slr_err;
    // Close output file
    con->output.close();
    // Close relay client
    slr_err = syslog_relay_client_free(con->syslog);
    if (slr_err != SYSLOG_RELAY_E_SUCCESS) return slr_err;
    // Close lockdown client
    lockdownd_error_t ldd_err = lockdownd_client_free(con->lockdown);
    if (ldd_err != LOCKDOWN_E_SUCCESS) return ldd_err;
    // Free device handle
    idevice_error_t idv_err = idevice_free(con->device);
    if (idv_err != IDEVICE_E_SUCCESS) return idv_err;
    // Delete connection
    connections.erase(udid);
    delete con;
    return 0;
}

void idevice_eventCallback(const idevice_event_t *event, void *user_data) {
    // Only accept connections over USB
    if (event->conn_type != 1) return;
    if (event->event == IDEVICE_DEVICE_ADD) {
        // Attempt to connect to new device
        std::cout << "Connecting to device " << event->udid << "...\n";
        int err = connectToDevice(event->udid);
        if (err == 0) std::cout << "Connected to " << event->udid << ".\n";
        else std::cout << "Failed to connect to " << event->udid << ": " << err << ".\n";
    } else if (event->event == IDEVICE_DEVICE_REMOVE) {
        // Attempt to disconnect from device
        std::cout << "Disconnecting from device " << event->udid << "...\n";
        int err = disconnectFromDevice(event->udid);
        if (err == 0) std::cout << "Disconnected from " << event->udid << ".\n";
        else std::cout << "Failed to disconnect from " << event->udid << ": " << err << ".\n";
    }
}

int usage(const char * argv0) {
    std::cout << "Usage: " << argv0 << " [OPTIONS]\nRead system logs from all connected iOS devices to a directory.\n\n\
    -o, --output DIRECTORY        output directory\n\
    -b, --buffer-size SIZE        number of bytes to read before flushing file\n\
    -h, --help                    show this help\n";
    return 0;
}

int main(int argc, const char * argv[]) {
    // Parse arguments
    int nextarg = 0;
    for (int i = 1; i < argc; i++) {
        switch (nextarg) {
        case 0:
            if (std::string(argv[i]) == "-o" || std::string(argv[i]) == "--output") nextarg = 1;
            else if (std::string(argv[i]) == "-b" || std::string(argv[i]) == "--buffer-size") nextarg = 2;
            else if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") return usage(argv[0]);
            break;
        case 1:
            log_cache_location = argv[i];
            nextarg = 0;
            break;
        case 2:
            try {
                max_buffer_size = std::stoul(std::string(argv[i]));
            } catch (std::exception &e) {
                return usage(argv[0]);
            }
            nextarg = 0;
            break;
        }
    }
    if (nextarg) return usage(argv[0]);
    // Listen for events
    idevice_event_subscribe(idevice_eventCallback, NULL);
    std::cout << "Waiting for devices...\n";
    // Main doesn't need to do anything since events run on a separate thread
    while (true) usleep(1000000);
}