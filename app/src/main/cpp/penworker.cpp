#include "penworker.h"

PenWorker::PenWorker() {
    ALOGD("PenWorker::PenWorker()");

    // Open the file
    std::ifstream inputFile("/proc/bus/input/devices");
    if (!inputFile.good()) {
        ALOGE("Failed to open /proc/bus/input/devices");
        throw std::runtime_error("Failed to open /proc/bus/input/devices");
    }

    std::string handler;
    std::string line;
    while (std::getline(inputFile, line)) {
        if (line.find("Vendor=056a") != std::string::npos &&
            line.find("Product=0000") != std::string::npos) {
            // Let's double-check we have the right device by checking the name
            std::getline(inputFile, line);
            if (line.find("Name=\"Wacom I2C Digitizer\"") != std::string::npos) {
                // We have the right device, so let's get the handler
                while (std::getline(inputFile, line)) {
                    if (line.find("H: Handlers=") != std::string::npos) {
                        // We have the handler, so let's get the device number
                        handler = line.substr(12, 6);
                        break;
                    }
                }
            }
        }
    }

    // Close the file
    inputFile.close();

    if (handler.empty()) {
        ALOGE("Failed to find the pen handler");
        throw std::runtime_error("Failed to find the pen handler");
    }

    std::string handlerPath = "/dev/input/" + handler;

    // Create a pipe for our child thread to read from
    if (pipe(pipefd) == -1) {
        ALOGE("Failed to create pipe");
        throw std::runtime_error("Failed to create pipe");
    }

    // Open the handler
    event_fd = open(handlerPath.c_str(), O_RDWR);
    if (event_fd < 0) {
        ALOGE("Failed to open %s with err: %s", handlerPath.c_str(), strerror(errno));
        throw std::runtime_error("Failed to open " + handlerPath +
                                 " for reading: " + strerror(errno));
    }

    // Start the polling thread
    penWorkerThread = std::thread(&PenWorker::run, this);
}

PenWorker::~PenWorker() {
    ALOGD("PenWorker::~PenWorker()");

    write(pipefd[1], "q", 1);
    if (penWorkerThread.joinable()) {
        penWorkerThread.join();
    }

    close(event_fd);
    close(pipefd[0]);
    close(pipefd[1]);
}

void PenWorker::run() {
    ALOGD("PenWorker::run()");
    int r;
    int fd_count = -1;

    auto pen_state = new pen_event_t();
    while (true) {
        FD_ZERO(&fds);
        FD_SET(pipefd[0], &fds);
        FD_SET(event_fd, &fds);

        if (event_fd > pipefd[0]) {
            fd_count = event_fd + 1;
        } else {
            fd_count = pipefd[0] + 1;
        }

        // Read the event
        struct input_event event{};
        r = select(fd_count, &fds, nullptr, nullptr, nullptr);
        if (r == -1) {
            ALOGE("select() failed with err: %s", strerror(errno));
            throw std::runtime_error("select() failed");
        }

        if (FD_ISSET(event_fd, &fds)) {
            if (read(event_fd, &event, sizeof(event)) != sizeof(event)) {
                ALOGE("Failed to read event with err: %s", strerror(errno));
                continue;
            }
        }
        if (FD_ISSET(pipefd[0], &fds)) {
            // The pipe has data, telling us to exit
            break;
        }

        // Check the event type
        if (event.type == EV_SYN) {
            if (pen_state->action == PEN_NOTHING && pen_state->pressure > 0) {
                pen_state->action = PEN_MOVE;
            }

            listener_mutex.lock();
            for (auto &listener: listeners) {
                // Send the data to the listener
                auto *listener_event = new pen_event_t;
                listener_event->x = pen_state->x;
                listener_event->y = pen_state->y;
                listener_event->pressure = pen_state->pressure;
                listener_event->action = pen_state->action;
                listener(listener_event);
            }
            listener_mutex.unlock();

            // Reset the pen state
            pen_state = new pen_event_t();
        } else if (event.type == EV_ABS) {
            switch (event.code) {
                case ABS_X:
                    pen_state->x = event.value;
                    break;
                case ABS_Y:
                    pen_state->y = event.value;
                    break;
                case ABS_PRESSURE:
                    pen_state->pressure = event.value;
                    break;
                default:
                    break;
            }
        } else if (event.type == EV_KEY) {
            if (event.code == BTN_TOOL_PEN) {
                pen_state->action = event.value ? PEN_DOWN : PEN_UP;
            }
        }
    }
}

void PenWorker::registerListener(const std::function<void(pen_event_t *)> &listener) {
    ALOGD("PenWorker::registerListener()");

    listener_mutex.lock();
    listeners.push_back(listener);
    listener_mutex.unlock();
}
