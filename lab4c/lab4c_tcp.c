//NAME: Luca Matsummoto
//ID: 204726167
//EMAIL: lucamatsumoto@gmail.com


#include <unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mraa.h>
#include <mraa/aio.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include "fcntl.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

//** REMEMBER TO CHANGE EXIT CODES
const int B = 4275;               // B value of the thermistor
const int R0 = 100000;            // R0 = 100k
int period = 1;
char flag = 'F';
struct pollfd polls[1];
int logfd;
int logFlag = 0;
int stopReports = 0;
int portNum;
int socketFd = 0;
struct sockaddr_in address;
struct hostent *server;
char* hostname = NULL;
char* idNum;
mraa_aio_context sensor;

void print_errors(char* error){
    if(strcmp(error, "temp") == 0){
        fprintf(stderr, "Failed to initialize temperature sensor\n");
        mraa_deinit();
        exit(1);
    }
    if(strcmp(error, "usage") == 0) {
        fprintf(stderr, "Incorrect argument: correct usage is ./lab4a --period=# [--scale=tempOpt] [--log=filename]\n");
        exit(1);
    }
    if(strcmp(error, "file") == 0) {
        fprintf(stderr, "Failed to create file\n");
        exit(2);
    }
    if(strcmp(error, "poll") == 0){
        fprintf(stderr, "Failed to poll\n");
        exit(2);
    }
    if(strcmp(error, "read") == 0){
        fprintf(stderr, "Failed to read\n");
        exit(2);
    }
    if(strcmp(error, "period") == 0){
        fprintf(stderr, "Period of 0 is not legal\n");
        exit(1);
    }
    if(strcmp(error, "socket") == 0){
        fprintf(stderr, "Error creating socket\n");
        exit(2);
    }
    if(strcmp(error, "connection") == 0){
        fprintf(stderr, "Error establishing connection to server\n");
        exit(2);
    }
    if(strcmp(error, "id_length") == 0){
        fprintf(stderr, "ID length is not 9. Inavlid ID\n");
        exit(1);
    }
    if(strcmp(error, "host") == 0){
        fprintf(stderr, "failed to get host name\n");
        exit(1);
    }
}

void create_report(double temp) {
    time_t raw;
    struct tm* currTime;
    time(&raw);
    currTime = localtime(&raw);
    dprintf(socketFd, "%.2d:%.2d:%.2d %.1f\n", currTime->tm_hour, currTime->tm_min, currTime->tm_sec, temp);
    if(logFlag && stopReports == 0) {
        dprintf(logfd, "%.2d:%.2d:%.2d %.1f\n", currTime->tm_hour, currTime->tm_min, currTime->tm_sec, temp);
    }
}//help with time from https://www.tutorialspoint.com/c_standard_library/c_function_localtime.htm

void initSensors(){
    sensor = mraa_aio_init(1);
    if(sensor == NULL){
        print_errors("temp");
    }
}

double getTemp(int tempReading){
    double  temp = 1023.0 / (double)tempReading - 1.0;
    temp *= R0;
    float temperature = 1.0/(log(temp/R0)/B+1/298.15) - 273.15;
    return flag == 'C'? temperature: temperature*9/5 + 32; //convert to Fahrenheit
}//algorithm from http://wiki.seeedstudio.com/Grove-Temperature_Sensor_V1.2/

void handle_scale(char scale) {
    switch(scale){
        case 'C':
        case 'c':
            flag = 'C';
            if(logFlag && stopReports == 0){
                dprintf(logfd, "SCALE=C\n");
            }
            break;
        case 'F':
        case 'f':
            flag = 'F';
            if(logFlag && stopReports == 0){
                dprintf(logfd, "SCALE=F\n");
            }
            break;
        default:
            fprintf(stderr, "Incorrect scale option");
            break;
    }
}

void handle_shutdown() {
    time_t raw;
    struct tm* currTime;
    time(&raw);
    currTime = localtime(&raw);
    dprintf(socketFd, "%.2d:%.2d:%.2d SHUTDOWN\n", currTime->tm_hour, currTime->tm_min, currTime->tm_sec);
    if(logFlag) {
        dprintf(logfd, "%.2d:%.2d:%.2d SHUTDOWN\n", currTime->tm_hour, currTime->tm_min, currTime->tm_sec);
    }
    exit(0);
}

void handle_off() {
    if(logFlag){
        dprintf(logfd, "OFF\n");
    }
    handle_shutdown();
}

void handle_period(int newPeriod) {
    period = newPeriod;
    if(logFlag && stopReports == 0){
        dprintf(logfd, "PERIOD=%d\n", period);
    }
}

void handle_stop() {
    stopReports = 1;
    if(logFlag){
        dprintf(logfd, "STOP\n");
    }
}

void handle_start() {
    stopReports = 0;
    if(logFlag){
        dprintf(logfd, "START\n");
    }
}

void handle_log(const char* input) {
    if(logFlag){
        dprintf(logfd, "%s\n", input);
    }
}


void handle_input(const char* input) {
    if(strcmp(input, "OFF") == 0){
        handle_off();
    }
    else if(strcmp(input, "START") == 0){
        handle_start();
    }
    else if(strcmp(input, "STOP") == 0){
        handle_stop();
    }
    else if(strcmp(input, "SCALE=F") == 0){
        handle_scale('F');
    }
    else if(strcmp(input, "SCALE=C") == 0){
        handle_scale('C');
    }
    else if(strncmp(input, "PERIOD=", sizeof(char)*7) == 0){
        int newPeriod = (int)atoi(input+7);
        handle_period(newPeriod);
    }
    else if((strncmp(input, "LOG", sizeof(char)*3) == 0)){
        handle_log(input);
    }
    else {
        fprintf(stderr, "Command not recognized\n");
        exit(1);
    }
}

void deinit_sensors(){
    mraa_aio_close(sensor);
    close(logfd);
}

void setupTCP() {
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketFd < 0){
        print_errors("socket");
    }
    server = gethostbyname(hostname);
    if (server == NULL){
        print_errors("host");
    } //check if hostname is retrieved
    memset((char*)&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    bcopy((char*)server->h_addr, (char*)&address.sin_addr.s_addr, server->h_length);
    address.sin_port = htons(portNum);
    if(connect(socketFd, (struct sockaddr*)&address, sizeof(address))< 0){
        print_errors("connection");
    }
}

void send_id() {
    setupTCP();
    dprintf(socketFd, "ID=%s\n", idNum);
    dprintf(logfd, "ID=%s\n", idNum);
}

void setupPollandTime(){
    char commandBuff[128];
    char copyBuff[128];
    memset(commandBuff, 0, 128);
    memset(copyBuff, 0, 128);
    int copyIndex = 0;
    polls[0].fd = socketFd;
    polls[0].events = POLLIN | POLLERR | POLLHUP;
    for(;;){
        int value = mraa_aio_read(sensor);
        double tempValue = getTemp(value);
        if(!stopReports){
            create_report(tempValue);
        }
        time_t begin, end;
        time(&begin);
        time(&end); //start begin and end at the same time and keep running loop until period is reached
        while(difftime(end, begin) < period){
            int ret = poll(polls, 1, 0);
            if(ret < 0){
                print_errors("poll");
            }
            if(polls[0].revents && POLLIN){
                int num = read(socketFd, commandBuff, 128);
                if(num < 0){
                    print_errors("read");
                }
                int i;
                for(i = 0; i < num && copyIndex < 128; i++){
                    if(commandBuff[i] =='\n'){
                        handle_input((char*)&copyBuff);
                        copyIndex = 0;
                        memset(copyBuff, 0, 128); //clear
                    }
                    else {
                        copyBuff[copyIndex] = commandBuff[i];
                        copyIndex++;
                    }
                }
                
            }
            time(&end);
        }
    }
}//help with time https://www.tutorialspoint.com/c_standard_library/c_function_time.htm

int main(int argc, char** argv){
    int opt = 0;
    static struct option options [] = {
        {"period", 1, 0, 'p'},
        {"scale", 1, 0, 's'},
        {"log", 1, 0, 'l'},
        {"id", 1, 0, 'i'},
        {"host", 1, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while((opt=getopt_long(argc, argv, "p:sl", options, NULL)) != -1){
        switch(opt){
            case 'p':
                period = (int)atoi(optarg);
                if(period == 0){
                    print_errors("period");
                }
                break;
            case 's':
                switch(*optarg){
                    case 'C':
                    case 'c':
                        flag = 'C';
                        break;
                    case 'F':
                    case 'f':
                        flag = 'F';
                        break;
                    default:
                        print_errors("usage");
                        break;
                }
                break;
            case 'l':
                logFlag = 1;
                char* logFile = optarg;
                logfd = creat(logFile, 0666);
                if(logfd < 0){
                    print_errors("file");
                }
                break;
            case 'i':
                if(strlen(optarg) != 9){
                    print_errors("id_length");
                }
                idNum = optarg;
                break;
            case 'h':
                hostname = optarg;
                break;
            default:
                print_errors("usage");
                break;
        }
    }
    portNum = atoi(argv[optind]);
    if(portNum <= 0){
        fprintf(stderr, "Invalid port number\n");
        exit(1);
    }
    close(STDIN_FILENO); //close input
    send_id();
    initSensors();
    setupPollandTime();
    deinit_sensors();
    exit(0);
}
//button is no longer used as a method of shutdown
