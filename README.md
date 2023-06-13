## 컴파일
gcc -o control1 control1.c -lpthread

gcc -o control2 control2.c

gcc -o game game.c -lpthread -lm -lwiringPi

gcc -o display display.c -lwiringPi

## 실행

1. ./game으로 메인 서버 실행

2. ./control1 <서버IP> <포트> - 컨트롤러1 연결 (기본 포트 8080)

3. ./control2 <서버IP> <포트> - 컨트롤러2 연결 (기본 포트 8081)

4. ./display <서버IP> <포트> - 디스플레이 연결 (기본 포트 8082)

## 데모 비디오

![](./DemoVideo_TEAM9.mp4)