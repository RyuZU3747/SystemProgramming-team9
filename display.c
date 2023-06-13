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

#define DIN		12
#define CLK		14
#define CS1		10
#define CS2		11

#define DECODE_MODE		0x09
#define INTENSITY		0x0a
#define SCAN_LIMIT		0x0b
#define SHUTDOWN		0x0c
#define DISPLAY_TEST	0x0f

#define BUFFER_SIZE 256

#define DISP_HEIGHT	16
#define DISP_WIDTH	32

unsigned char dotMatrix[2][4][8];

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

void send_SPI_16bits(unsigned short data)
{
	for (int i = 16; i > 0; i--)
	{
		unsigned short mask = 1 << (i - 1);
		
		digitalWrite(CLK, 0);
		digitalWrite(DIN, (data & mask) ? 1 : 0);
		digitalWrite(CLK, 1);
	}
}

void send_MAX7219(unsigned short address, unsigned short data)
{
	send_SPI_16bits((address << 8) + data);
}

void init_MAX7219(int cs, unsigned short address, unsigned short data)
{
	for (int i = 0; i < 4; i++)
	{
		
		digitalWrite(cs,LOW);
		send_MAX7219(address, data);
		digitalWrite(cs,HIGH);
	}
}

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

void update_Matrix()
{
	int cs[2] = { CS1, CS2 };
	
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
	pinMode(CS1, OUTPUT);
	
	init_MAX7219(CS1, DECODE_MODE, 0x00);
	init_MAX7219(CS1, INTENSITY, 0x01);
	init_MAX7219(CS1, SCAN_LIMIT, 0x07);
	init_MAX7219(CS1, SHUTDOWN, 0x01);
	init_MAX7219(CS1, DISPLAY_TEST, 0x00);
	
	init_MAX7219(CS2, DECODE_MODE, 0x00);
	init_MAX7219(CS2, INTENSITY, 0x01);
	init_MAX7219(CS2, SCAN_LIMIT, 0x07);
	init_MAX7219(CS2, SHUTDOWN, 0x01);
	init_MAX7219(CS2, DISPLAY_TEST, 0x00);

	sleep(1);

    if (argc != 3)
    {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");
	
	
	int ball_y, ball_x;
	int p1_y, p1_x, p1_l;
	int p2_y, p2_x, p2_l;
	
	while (1)
	{
		read(sock, buf, sizeof(buf));
		//printf("%d %d %d %d %d %d %d %d\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
		ball_y = buf[0];
		ball_x = buf[1];
		p1_y = buf[2];
		p1_x = buf[3];
		p1_l = buf[4];
		p2_y = buf[5];
		p2_x = buf[6];
		p2_l = buf[7];
		
		memset(dotMatrix, 0, sizeof(dotMatrix));
		
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
		
		update_Matrix();
	
		usleep(10000);
	}
	
	return (0);
}
