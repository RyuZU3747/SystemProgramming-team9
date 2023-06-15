#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <wiringPi.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// pin 번호
#define DIN		12
#define CLK		14
#define CS0		10
#define CS1		11

#define DECODE_MODE		0x09
#define INTENSITY		0x0a
#define SCAN_LIMIT		0x0b
#define SHUTDOWN		0x0c
#define DISPLAY_TEST	0x0f

#define BUFFER_SIZE 256

// 디스플레이 크기 16 * 32
#define DISP_HEIGHT	16
#define DISP_WIDTH	32

// dotMatrix[cs][slave][row]
unsigned char dotMatrix[2][4][8];

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

// SPI 통신으로 16bit 전송
void send_SPI_16bits(unsigned short data)
{
	for (int i = 16; i > 0; i--)
	{
		unsigned short mask = 1 << (i - 1);
		// Clock을 조절하면서 데이터 전송
		digitalWrite(CLK, 0);
		digitalWrite(DIN, (data & mask) ? 1 : 0);
		digitalWrite(CLK, 1);
	}
}

// MAX7219 Serial-Data Format(16bit)에 맞게 전송
// [address(8bit)][data(8bit)]
void send_MAX7219(unsigned short address, unsigned short data)
{
	send_SPI_16bits((address << 8) + data);
}

// 초기 설정값을 보내는 함수
void init_MAX7219(int cs, unsigned short address, unsigned short data)
{
    // Daisy-Chain방식으로 연결된 4개의 MAX7219에 데이터를 쓰기 위해 for문에서 64bit(16 * 4) 데이터를 보낸 후 CS핀을 HIGH로 활성화하였다.
	digitalWrite(cs,LOW);
	for (int i = 0; i < 4; i++)
		send_MAX7219(address, data);
	digitalWrite(cs,HIGH);
}

// 게임 상에서의 x y 좌표를 도트매트릭스 제어에 맞게 변환하여 값을 세팅해준다.
void set_Matrix(int y, int x)
{
	int cs;
	int ss;
	int row;
	int col;
	
	cs = y / 8;
	ss = x / 8;
	row = y % 8;
	col = x % 8;
	
	dotMatrix[cs][ss][row] |= 1 << (7 - col);
}

// dotMatrix에 맞게 도트매트릭스에 출력해준다.
void update_Matrix()
{
	int cs[2] = { CS0, CS1 };
	
	for (int k = 0; k < 2; k++)
	{
		for( int i = 1 ; i < 9 ; i++)
		{
		  digitalWrite(cs[k],LOW);
		  for( int j = 0 ; j < 4 ; j++)
		  {
			  send_MAX7219(i, dotMatrix[k][j][i - 1]);
		  }
		  digitalWrite(cs[k],HIGH);
		}
	}
}

void intHandler(int dummy)
{
	send_MAX7219(SHUTDOWN, 0);
	exit(0);
}

int main(int argc, char **argv)
{
	int sock;
    struct sockaddr_in serv_addr;
    char buf[BUFFER_SIZE];
    
	signal(SIGINT, intHandler);
	
	if (wiringPiSetup() < 0)
	{
		return 1;
	}
	
	pinMode(DIN, OUTPUT);
	pinMode(CLK, OUTPUT);
	pinMode(CS0, OUTPUT);
	
    // MAX7219 초기 설정
	init_MAX7219(CS0, DECODE_MODE, 0x00); // Decode Mode - No decode for digits
	init_MAX7219(CS0, INTENSITY, 0x01); // Intensity - 1/32
	init_MAX7219(CS0, SCAN_LIMIT, 0x07); // Scan Limit - All Output Port Enable
	init_MAX7219(CS0, SHUTDOWN, 0x01); // Shutdown - Normal Operation
	init_MAX7219(CS0, DISPLAY_TEST, 0x00); // Display Test
	
	init_MAX7219(CS1, DECODE_MODE, 0x00);
	init_MAX7219(CS1, INTENSITY, 0x01);
	init_MAX7219(CS1, SCAN_LIMIT, 0x07);
	init_MAX7219(CS1, SHUTDOWN, 0x01);
	init_MAX7219(CS1, DISPLAY_TEST, 0x00);

	sleep(1);

    if (argc != 3)
    {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    // 서버와 통신할 socket 생성
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    // 서버와 연결
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");
	
	int ball_y, ball_x;
	int p1_y, p1_x, p1_l;
	int p2_y, p2_x, p2_l;
	
	while (1)
	{
        // 서버로 부터 데이터 받아오기
		read(sock, buf, sizeof(buf));
		ball_y = buf[0]; // 볼 y좌표
		ball_x = buf[1]; // 볼 x좌표
		p1_y = buf[2]; // 플레이어1 y좌표
		p1_x = buf[3]; // 플레이어1 x좌표
		p1_l = buf[4]; // 플레이어1 막대길이
		p2_y = buf[5]; // 플레이어2 y좌표
		p2_x = buf[6]; // 플레이어2 x좌표
		p2_l = buf[7]; // 플레이어2 막대길이
		
		memset(dotMatrix, 0, sizeof(dotMatrix));
		
        // dotMatrix에 플레이어 막대와 볼 표시하기
		for (int i = 0; i < DISP_HEIGHT; i++)
		{
			for (int j = 0; j < DISP_WIDTH; j++)
			{
				if (i == ball_y && j == ball_x)
					set_Matrix(i, j);
				else if (p1_y <= i && i < p1_y + p1_l && j == p1_x)
					set_Matrix(i, j);
				else if (p2_y <= i && i < p2_y + p2_l && j == p2_x)
					set_Matrix(i, j);
			}
		}
		
        // 실제 도트매트릭스에 출력하기
		update_Matrix();
	
        // 프레임 조절
		usleep(10000);
	}
	
	return (0);
}
