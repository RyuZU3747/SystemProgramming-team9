## 컴파일
gcc -o control1 control1.c -lpthread

gcc -o control2 control2.c -lpthread

gcc -o game game.c -lpthread -lm -lwiringPi

gcc -o display display.c -lpthread -lwiringPi

## 실행

1. ./game으로 메인 서버 실행

2. ./control1 <서버IP> <포트>로 컨트롤러 연결 (기본 포트 8080)

3. ./control2 <서버IP> <포트>로 컨트롤러 연결 (기본 포트 8081)

4. ./display <서버IP> <포트>로 컨트롤러 연결 (기본 포트 8082)