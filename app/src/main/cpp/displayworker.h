//
// Created by mulliken on 5/17/22.
//
#ifndef DISPLAYWORKER_H
#define DISPLAYWORKER_H

#include "const.h"
#include "pinenotelib.h"
#include "raster_utils.h"

#include <queue>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include <csignal>
#include <fstream>
#include <android/log.h>
#include <thread>

#define PEN_RADIUS 3
#define PEN_COLOR 0x00 // black

using namespace std;

class DisplayWorker {
public:
    DisplayWorker();

    ~DisplayWorker();

    void onPenEvent(pen_event_t pen_event);

private:
    PineNoteLib *mPineNoteLib;

    queue<pen_event_t> equeue{};
    mutex emutex;
    condition_variable econd;

    thread display_thread;

    pen_event_t prev_event{};

    void run();
};

#endif //DISPLAYWORKER_H