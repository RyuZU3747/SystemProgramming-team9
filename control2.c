#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/socket.h>
#include <string.h>

#define Device_Address 0x68  // MPU6050의 I2C 주소

#define PWR_MGMT_1   0x6B
#define SMPLRT_DIV   0x19
#define CONFIG       0x1A
#define GYRO_CONFIG  0x1B
#define INT_ENABLE   0x38
#define ACCEL_XOUT_H 0x3B
#define ACCEL_YOUT_H 0x3D
#define ACCEL_ZOUT_H 0x3F
#define GYRO_XOUT_H  0x43
#define GYRO_YOUT_H  0x45
#define GYRO_ZOUT_H  0x47

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1
#define PIN 14
#define VALUE_MAX 40

int file;

void error_handling(char *message){
    fputs(message,stderr);
    fputc('\n',stderr);
    exit(1);
}

void MPU_Init() {
    // I2C 
    char buf[2];
    char *i2cDevice = "/dev/i2c-1";  // I2C 장치 파일 경로
    if ((file = open(i2cDevice, O_RDWR)) < 0) { //I2C 파일 open
        printf("Unable to open I2C device.\n");
        exit(1);
    }

    // MPU6050으로부터 데이터 읽기 설정
    if (ioctl(file, I2C_SLAVE, Device_Address) < 0) {
        printf("Unable to select I2C device.\n");
        close(file);
        exit(1);
    }
    buf[0] = SMPLRT_DIV;
    buf[1] = 7;
    write(file, buf, 2);

    // Disable sleep mode (wake up)
    buf[0] = PWR_MGMT_1;
    buf[1] = 1;
    write(file, buf, 2);

    // Disable FSYNC and set accelerometer and gyro bandwidth to 44 and 42 Hz, respectively
    buf[0] = CONFIG;
    buf[1] = 0;
    write(file, buf, 2);

    // Set gyro full scale range of +/-2000 degrees per second
    buf[0] = GYRO_CONFIG;
    buf[1] = 24;
    write(file, buf, 2);

    // Enable interrupt
    buf[0] = INT_ENABLE;
    buf[1] = 1;
    write(file, buf, 2);
    /*
    // I2C 
    char *i2cDevice = "/dev/i2c-1";  // I2C 장치 파일 경로 (Raspberry Pi 3 이상)
    if ((file = open(i2cDevice, O_RDWR)) < 0) {
        printf("Unable to open I2C device.\n");
        exit(1);
    }

    // MPU6050으로부터 데이터 읽기 설정
    if (ioctl(file, I2C_SLAVE, Device_Address) < 0) {
        printf("Unable to select I2C device.\n");
        close(file);
        exit(1);
    }

    // Sample Rate = 8kHz/(1 + SMPLRT_DIV)
    write(file, &(unsigned char){SMPLRT_DIV}, 1);
    write(file, &(unsigned char){7}, 1);

    // Disable sleep mode (wake up)
    write(file, &(unsigned char){PWR_MGMT_1}, 1);
    write(file, &(unsigned char){0}, 1);

    // Disable FSYNC and set accelerometer and gyro bandwidth to 44 and 42 Hz, respectively
    write(file, &(unsigned char){CONFIG}, 1);
    write(file, &(unsigned char){0}, 1);

    // Set gyro full scale range of +/-2000 degrees per second
    write(file, &(unsigned char){GYRO_CONFIG}, 1);
    write(file, &(unsigned char){24}, 1);

    // Enable interrupt
    write(file, &(unsigned char){INT_ENABLE}, 1);
    write(file, &(unsigned char){1}, 1);*/
}

int read_raw_data(int addr) {
    unsigned char high, low;
    int value;

    // 레지스터 주소 전송
    write(file, &(unsigned char){addr}, 1);

    // 데이터 읽기
    read(file, &high, 1);
    read(file, &low, 1);

    value = (high << 8) | low;

    // Convert to signed value if necessary
    if (value > 32768) {
        value = value - 65536;
    }

    return value;
}

static int GPIOExport(int pin){
    #define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if(-1 == fd)
    {
        fprintf(stderr, "Failed to open export for writing!\n");
        return(-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return(0);
}

static int GPIOUnexport(int pin){
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport",O_WRONLY);
    if(-1 == fd){
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return(-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return(0);
}

static int GPIODirection(int pin, int dir){ //GPIO 설정정
    static const char s_directions_str[] = "in\0out";
    #define DIRECTION_MAX 35
    char path[DIRECTION_MAX] = "/sys/class/gpio/gpio%d/direction";
    int fd;
    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if(-1 == fd){
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return(-1);
    }
    if(-1 == write(fd, &s_directions_str[IN == dir? 0 : 3], IN == dir? 2 : 3)){
        fprintf(stderr, "failed to set direction!\n");
        close(fd);
        return(-1);
    }

    close(fd);
    return(0);
}

static int GPIORead(int pin){ // GPIO 센서 읽기기
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if(-1 == fd){
        fprintf(stderr, "Failed to open for read\n");
        return(-1);
    }
    if(-1 == read(fd, value_str, 3)){
        fprintf(stderr, "failed to read value\n");
        close(fd);
        return(-1);
    }
    close(fd);
    return(atoi(value_str));
}

int is_touched()
{
    return GPIORead(PIN); // 터치 센서 값 읽기기
}

int gyroZ() //필요한 센서값만 추출출
{
    int gyro_z = read_raw_data(GYRO_ZOUT_H);
    float Gz = gyro_z / 131.0; // 약약 -40 ~ 40
    if(Gz < -30) // 센서값 확인 및 형식화화
    {
        return -2;
    }
    else if(Gz < -5)
    {
        return -1;
    }
    else if(Gz < 5)
    {
        return 0;
    }
    else if(Gz < 30)
    {
        return 1;
    }
    else
    {
        return 2;
    }
}

void gyro_all() //모든  센서값 읽기기
{
        int acc_x = read_raw_data(ACCEL_XOUT_H);
        int acc_y = read_raw_data(ACCEL_YOUT_H);
        int acc_z = read_raw_data(ACCEL_ZOUT_H);
        int gyro_x = read_raw_data(GYRO_XOUT_H);
        int gyro_y = read_raw_data(GYRO_YOUT_H);
        int gyro_z = read_raw_data(GYRO_ZOUT_H);

        float Ax = acc_x / 16384.0;
        float Ay = acc_y / 16384.0;
        float Az = acc_z / 16384.0;
        float Gx = gyro_x / 131.0;
        float Gy = gyro_y / 131.0;
        float Gz = gyro_z / 131.0;
        
        printf("Gx=%.2f°/s\tGy=%.2f°/s\tGz=%.2f°/s\tAx=%.2f g\tAy=%.2f g\tAz=%.2f g\n",
               Gx, Gy, Gz, Ax, Ay, Az);
}

int setupGPIO() //GPIO 설정정
{
    if(-1 == GPIOExport(PIN))
    {
        return(1);
    }
    if(-1 == GPIODirection(PIN, IN))
    {
        return(2);
    }
    return 0;
}

void print_bar(int num, int touched) //test용 print 함수수
{
    int bar = 4, size = 5;
    if(touched)
    {
        for(int i=0;i<2;i++)
        {
            for(int j=0;j<bar*size;j++)
            {
                printf("■");
            }
            printf("\n");
        }
        return;
    }
    int which = 2 + num;
    for(int i=0;i<2;i++)
    {
        for(int j=0;j<size;j++)
        {
            if(which == j)
            {
                printf("■■■■");
            }
            else
            {
                printf("□□□□");
            }
        }
        printf("\n");
    }
    return;
}

int main(int argc, char** argv) {
    int sock;
    struct sockaddr_in serv_addr;
    int str_len;
    int vel = 0;
    char msg[5] = "0000"; // "Velocity to up, down, istouched, 0"
    
    if(argc!=3){
        printf("Usage : %s <IP> <port>\n",argv[0]);
        exit(1);
    }
    
    MPU_Init();
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if(sock == -1)
        error_handling("socket() error");
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));  
    
    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1)
        error_handling("connect() error");
        
    if(setupGPIO())
    {
        printf("GPIO SETUP ERR\n");
        return 1;
    }
    printf("Reading Data of Gyroscope and Accelerometer\n");
    while (1) {
		printf("touched : %d, Gyroscope: Z=%d\n", is_touched(), -1 * gyroZ());
        vel = gyroZ();
        if(vel < 0)
        {
            msg[0] = '0' + -1 * vel;
            msg[1] = '0';
        }
        else
        {
            msg[0] = '0';
            msg[1] = '0' + vel;
        }
        msg[2] = '0' + is_touched();
        printf("msg : %s\n", msg);
        write(sock, msg, strlen(msg));
        //print_bar(-1 * gyroZ(), is_touched());
        usleep(30000);
    }
    if(-1 == GPIOUnexport(PIN))
    {
        return(4);
    }
    close(file);
    return 0;
}
