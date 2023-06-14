#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <pthread.h>

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1
#define CHOOUT 23
#define CHOIN 24
#define UPBUT 17
#define DOWNBUT 27
#define TOUCHBUT 22
#define VALUE_MAX 40
#define swap(a,b) {int c;c=a;a=b;b=c;}

double distance = 0;
int sock;
struct sockaddr_in serv_addr;
int inputsize = 1;
char sendinfo[5] = {'0','0','0','0','\0'};//각각 위, 아래, 터치, 초음파

static int GPIOExport(int pin) {
#define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open export for writing!\n");
        return -1;
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return 0;
}

static int GPIODirection(int pin, int dir){
    static const char s_directions_str[] = "in\0out";

#define DIRECTION_MAX 35
    char path[DIRECTION_MAX] = "/sys/class/gpio/gpio%d/direction";
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);

    fd = open(path, O_WRONLY);
    if (-1 == fd){
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return -1;
    }

    if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)){
        fprintf(stderr, "Failed to set direction!\n");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int GPIOWrite(int pin, int value){
    static const char s_values_str[] = "01";

    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd){
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return -1;
    }

    if(1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)){
        fprintf(stderr, "Failed to write value!\n");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int GPIOUnexport(int pin){
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (-1 == fd){
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return -1;
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return 0;
}

static int GPIORead(int pin) {
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return -1;
    }

    if (-1 == read(fd, value_str, 3)){
        fprintf(stderr, "Failed to read value!\n");
        close(fd);
        return -1;
    }

    close(fd);
    return atoi(value_str);
}

void *cho_umpa(){ //초음파 스레드용 포인터함수
    clock_t start_t, end_t;
    double time;
    if(GPIOExport(CHOOUT)==-1 || GPIOExport(CHOIN)==-1){
        printf("gpio export err\n");
        exit(0);
    }
    usleep(100000);
    if(GPIODirection(CHOOUT, OUT)==-1||GPIODirection(CHOIN,IN)==-1){
        printf("gpio direction err\n");
        exit(0);
    }
    GPIOWrite(CHOOUT,0);
    usleep(10000);
    double beforedistance = 0; //이전 인식된 거리
    while(1){
        if(GPIOWrite(CHOOUT,1)==-1){
            printf("gpio write/trigger err\n");
            exit(0);
        }
		usleep(10);
        GPIOWrite(CHOOUT, 0);
        while(GPIORead(CHOIN)==0) start_t = clock();
        while(GPIORead(CHOIN)==1) end_t = clock();
        time = (double)(end_t-start_t)/CLOCKS_PER_SEC;
        distance = time/2*34000;
        if(distance > 100) distance = 100; //100cm이상은 고정
        double distance_delta = distance - beforedistance; // 이전 거리와의 차이로 컨트롤러의 움직임을 감지
        
        /*센서 확인용 출력*/
		//printf("cur : %.2fcm before : %2.fcm delta : %.2fcm\n",distance,beforedistance,distance_delta);
        
        beforedistance = distance; //이전 거리 저장
        if(abs(distance_delta) > 5){ //만약 컨트롤러가 움직인 거리가 5cm 이상이면 (절댓값은 있어도 되고 없어도 된다)
            sendinfo[3] = '1'; //초음파에 해당되는 부분을 켜준다
        }
        else sendinfo[3] = '0';
		usleep(500000);
    }
}

void *buttonfunc(){ //위아래 버튼용 포인터함수
    if(GPIOExport(UPBUT)==-1 || GPIOExport(DOWNBUT)==-1){
        printf("gpio export err\n");
        exit(0);
    }
    usleep(100000);
    if(GPIODirection(UPBUT, IN)==-1||GPIODirection(DOWNBUT, IN)==-1){
        printf("gpio direction err\n");
        exit(0);
    }
    while(1){
        int upbut = GPIORead(UPBUT);
        int downbut = GPIORead(DOWNBUT);
        if(upbut==1){
            sendinfo[0] = '1';
        }
        else sendinfo[0] = '0';
        if(downbut==1){
            sendinfo[1] = '1';
        }
        else sendinfo[1] = '0';
    }
}

void *touchfunc(){ //터치 센서용 포인터함수
    if(GPIOExport(TOUCHBUT)==-1){
        printf("gpio export err\n");
        exit(0);
    }
    usleep(100000);
    if(GPIODirection(TOUCHBUT, IN)==-1){
        printf("gpio direction err\n");
        exit(0);
    }
    while(1){
        int touchbut = GPIORead(TOUCHBUT);
        if(touchbut==1){
            sendinfo[2] = '1';
        }
        else sendinfo[2] = '0';
    }
}

void *tongshin(){ //통신 담당 포인터함수
    while(1){
        /*확인용 출력*/
		//printf("%s\n",sendinfo);

		usleep(10000); //0.01초 즉 100프레임의 레이트로 데이터를 쓴다
        write(sock, sendinfo, 5);
    }
}

int main(int argc, char *argv[]){    
    pthread_t cho_thread;
    int chothr;
    pthread_t but_thread;
    int butthr;
    pthread_t tch_thread;
    int tchthr;
    pthread_t con_thread;
    int conthr;
    int status;

    if (argc != 3)
    {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }
    
    sock = socket(PF_INET, SOCK_STREAM, 0);
    
    if(sock == -1)
        printf("socket() error\n");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));
    
	if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1) // 연결된 이후에 스레드를 나눠준다
        printf("connect() error");
        
    /*역할별 스레드 분리*/
    chothr = pthread_create(&cho_thread, NULL, cho_umpa, 0);
    if(chothr < 0){
        perror("thread create error : ");
        exit(0);
    }
    butthr = pthread_create(&but_thread, NULL, buttonfunc, 0);
    if(butthr < 0){
        perror("thread create error : ");
        exit(0);
    }
    tchthr = pthread_create(&tch_thread, NULL, touchfunc, 0);
    if(tchthr < 0){
        perror("thread create error : ");
        exit(0);
    }
    conthr = pthread_create(&con_thread, NULL, tongshin, 0);
    if(conthr < 0){
        perror("thread create error : ");
        exit(0);
    }
    
    pthread_join(con_thread, (void**)&status); // 만약 통신용 스레드가 멈추면 cancel을 통해 전부 멈춘다
    pthread_cancel(cho_thread);
    pthread_cancel(but_thread);
    pthread_cancel(tch_thread);
    
    if (GPIOUnexport(UPBUT) == -1||GPIOUnexport(DOWNBUT) == -1||GPIOUnexport(TOUCHBUT) == -1||GPIOUnexport(CHOIN) == -1||GPIOUnexport(CHOOUT) == -1)
        return 4;
    
    return 0;
}
