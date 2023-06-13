#include <arpa/inet.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// 연결 포트

#define CTRL1_PORT 8080
#define CTRL2_PORT 8081
#define DISP_PORT 8082

// 게임 파라미터
#define GAME_FPS 60          // 초당 게임 프레임 수
#define DISP_FPS 30          // 초당 디스플레이 프레임 수
#define INPUT_DELAY_us 10000 // 컨트롤러 입력 처리 주기 (마이크로초 단위)
#define SCALE 100            // 속도 단위

// 개발용
#define DISABLE_SOCK 0    // 컨트롤러 없이 게임 실행
#define DISABLE_DISP 0    // 도트 매트릭스 출력 안함
#define DISABLE_LCD 0     // LCD 출력 안함
#define DISPLAY_CONSOLE 1 // 콘솔 출력 여부

// 플레이 요소
#define GAME_TIME 60       // 게임 시간 (초)
#define BALL_SPEED 20      // 1 = 프레임당 0.01픽셀
#define PADDLE_SPEED 50    // 기본 막대 속도
#define PADDLE_REFLECT 100 // 막대에 맞은 공의 반사 속도 계수 (%)
#define PLAYER_POS 1       // 끝에서 N칸 떨어진 위치
#define PLAYER_LEN 5       // 기본 막대 길이
#define ULT_FRAME 120      // 궁극기 지속 프레임

// 고정값
#define DISP_HEIGHT 16 // 가로로 눕힌 도트 매트릭스로 가정
#define DISP_WIDTH 32

// 자동 계산되는 값
#define MAX_GAME_FRAME (GAME_FPS * GAME_TIME)   // 총 게임 프레임
#define FRAME_TIME_us (1000000 / GAME_FPS)      // 마이크로초 단위
#define DISP_FRAME_TIME_us (1000000 / DISP_FPS) // 마이크로초 단위
#define HEIGHT (SCALE * DISP_HEIGHT)            // 내부 처리용 단위
#define WIDTH (SCALE * DISP_WIDTH)              //
#define PADDLE_POS (SCALE * PLAYER_POS)         // 막대 위치 내부값
#define PADDLE_LEN (SCALE * PLAYER_LEN)         // 막대 길이 내부값

// 디스플레이 관련, DISABLE_LCD==0일 때만 사용
#if DISABLE_LCD == 0

#include <wiringPi.h>
#include <wiringPiI2C.h>

#define I2C_ADDR 0x27 // I2C 연결 주소
#define LCD_CHR 1     // 데이터 전송 모드
#define LCD_CMD 0     // 명령 전송 모드
#define LINE1 0x80    // 첫번째 줄
#define LINE2 0xC0    // 2두번쨰 줄

#define LCD_BACKLIGHT 0x08 // 백라이트 ON

#define ENABLE 0b00000100 // 활성화 비트

int lcd_fd; // LCD fd

void lcd_toggle_enable(int bits) {
    delayMicroseconds(500);
    wiringPiI2CReadReg8(lcd_fd, (bits | ENABLE));
    delayMicroseconds(500);
    wiringPiI2CReadReg8(lcd_fd, (bits & ~ENABLE));
    delayMicroseconds(500);
}

void lcd_byte(int bits, int mode) {
    int bits_high = mode | (bits & 0xF0) | LCD_BACKLIGHT;
    int bits_low = mode | ((bits << 4) & 0xF0) | LCD_BACKLIGHT;

    wiringPiI2CReadReg8(lcd_fd, bits_high);
    lcd_toggle_enable(bits_high);

    wiringPiI2CReadReg8(lcd_fd, bits_low);
    lcd_toggle_enable(bits_low);
}

// 글자 출력
void typeln(const char *s) {
    while (*s)
        lcd_byte(*(s++), LCD_CHR);
}

void lcd_clear(void) {
    lcd_byte(0x01, LCD_CMD);
    lcd_byte(0x02, LCD_CMD);
}

void lcdLoc(int line) {
    lcd_byte(line, LCD_CMD);
}

void typeChar(char val) {
    lcd_byte(val, LCD_CHR);
}

void lcd_init() {
    lcd_byte(0x33, LCD_CMD); // 초기화
    lcd_byte(0x32, LCD_CMD); // 초기화
    lcd_byte(0x06, LCD_CMD); // 커서 이동 방식
    lcd_byte(0x0C, LCD_CMD); // 화면 On, 커서 Off
    lcd_byte(0x28, LCD_CMD); // 2줄 표시
    lcd_byte(0x01, LCD_CMD); // Clear
    delayMicroseconds(500);
}

#endif

typedef struct {
    int h;
    int w;
    int vh;
    int vw;        // 속도
    int boost_cnt; // 남은 부스터 카운트
} Ball;

typedef struct {
    int h;
    int w;              // 고정값
    int paddle_len;     // 막대 길이
    int paddle_v;       // 막대 속도
    int paddle_reflect; // 막대에 맞은 공의 반사 속도 계수
    int ult_cnt;        // 남은 궁극기 카운트
    int score;
} Player;

typedef struct {
    int frame;    // 프레임 카운터
    int gameover; // 게임 오버 플래그

    Ball ball;
    Player player1;
    Player player2;
} GameState;

// 내부 단위를 픽셀 단위로 변환
int translate_dot(int x) {
    return x / SCALE;
}

void reset_ball(GameState *state) {
    // 공 위치 초기화
    state->ball.h = HEIGHT / 2;
    state->ball.w = WIDTH / 2;
    int ball_v = BALL_SPEED;
    srand(time(NULL));
    int ball_angle = ((rand() % 91) + 45) + ((rand() % 2) * 180); // 45도 ~ 135도, 225도 ~ 315도
    while (ball_angle % 90 < 10)
        ball_angle = ((rand() % 91) + 45) + ((rand() % 2) * 180); // 지나치게 수평/수직이면 다시 뽑기
    state->ball.vh = ball_v * cos(M_PI / 180.0 * ball_angle);
    state->ball.vw = ball_v * sin(M_PI / 180.0 * ball_angle);

    // 공 부스트 초기화
    state->ball.boost_cnt = 0;
}

int sock_listen;                  // 소켓 연결 스레드 종료 플래그
int ctrl1_connect, ctrl2_connect; // 컨트롤러 연결 여부
int ctrl1_v, ctrl2_v;             // 컨트롤러 막대 속도
int ctrl1_ult, ctrl2_ult;         // 컨트롤러 궁극기 입력
int ctrl1_rcv;                    // 컨트롤러1 초음파 반사

/* handle_ctrl()
 * 컨트롤러 연결 처리 쓰레드
 */
void *handle_ctrl(void *arg) {
    int port = *(int *)arg;
    int server_fd;

    // 라즈베리파이 소켓 설정
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Error opening socket for controller");
        pthread_exit(NULL);
    }

    if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Error binding socket for controller");
        close(server_fd);
        pthread_exit(NULL);
    }

    listen(server_fd, 5);

    // 컨트롤러 연결 대기
    int client_fd;
    struct sockaddr_in client_address;
    socklen_t client_address_size = sizeof(client_address);
    client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_address_size);
    if (client_fd < 0) {
        perror("Error accepting controller connection");
        close(server_fd);
        pthread_exit(NULL);
    }

    // 컨트롤러 연결 완료
    if (CTRL1_PORT == port) {
        ctrl1_connect = 1;
        printf("Player 1 connected\n");
    } else {
        ctrl2_connect = 1;
        printf("Player 2 connected\n");
    }

    char msg[5] = { 0 }; // 0000 [UP][DOWN][궁극기][초음파]
    int str_len = 0;
    printf("%s\n", msg);
    while (sock_listen) {
        str_len = read(client_fd, msg, sizeof(msg));
        if (CTRL1_PORT == port) {
            ctrl1_v = (msg[1] - '0') - (msg[0] - '0');
            ctrl1_ult = msg[2] - '0';
            ctrl1_rcv = msg[3] - '0';
        } else {
            ctrl2_v = (msg[1] - '0') - (msg[0] - '0');
            ctrl2_ult = msg[2] - '0';
        }
        usleep(INPUT_DELAY_us);
    }

    close(client_fd);
    close(server_fd);
    pthread_exit(NULL);
}

/* init_game()
 * 게임 변수 초기화
 */
int init_game(GameState *state) {
    state->frame = 0;
    state->gameover = 0;

    // 공 위치 초기화
    reset_ball(state);

    // 플레이어 1 (좌측) 스탯 초기화
    // TODO: 플레이어별 스탯 차별화
    state->player1.h = HEIGHT / 2 - PADDLE_LEN / 2;
    state->player1.w = PADDLE_POS;
    state->player1.paddle_len = PADDLE_LEN;
    state->player1.paddle_v = PADDLE_SPEED;
    state->player1.paddle_reflect = PADDLE_REFLECT;
    state->player1.ult_cnt = 0;
    state->player1.score = 0;

    // 플레이어 2 (우측) 스탯 초기화
    // TODO: 플레이어별 스탯 차별화
    state->player2.h = HEIGHT / 2 - PADDLE_LEN / 2;
    state->player2.w = WIDTH - PADDLE_POS - 1;
    state->player2.paddle_len = PADDLE_LEN;
    state->player2.paddle_v = PADDLE_SPEED;
    state->player2.paddle_reflect = PADDLE_REFLECT;
    state->player2.ult_cnt = 0;
    state->player2.score = 0;

    return 0;
}

/* get_input()
 * 컨트롤러로부터 입력 수신
 */
int get_input(GameState *state) {
    state->player1.paddle_v = ctrl1_v * PADDLE_SPEED;
    state->player1.ult_cnt += state->player1.ult_cnt ? 0 : ctrl1_ult * ULT_FRAME;
    state->player1.paddle_reflect = PADDLE_REFLECT * (1 + ctrl1_rcv * 2);
    state->player2.paddle_v = ctrl2_v * PADDLE_SPEED;
    state->player2.ult_cnt += state->player2.ult_cnt ? 0 : ctrl2_ult * ULT_FRAME;
    return 0;
}

/* check_gameover()
 * 게임 오버 체크
 */
int check_gameover(GameState *state) {
    if (state->frame >= MAX_GAME_FRAME) {
        state->gameover = 1;
    }
    return 0;
}

// TODO: 적절하게 함수로 분리
/* update_game()
 * 게임 프레임 업데이트
 */
int update_game(GameState *state) {
    state->frame++;

    get_input(state);

    // 공 위치 업데이트
    state->ball.h += state->ball.vh;
    state->ball.w += state->ball.vw;

    // P2 궁극기 패들 길이증가
    if (state->player2.ult_cnt)
        state->player2.paddle_len = PADDLE_LEN * 2;
    else
        state->player2.paddle_len = PADDLE_LEN;

    // 플레이어 위치 업데이트
    state->player1.h += state->player1.paddle_v;
    state->player2.h += state->player2.paddle_v;
    // 벗어나지 않도록 제한
    if (state->player1.h < 0) {
        state->player1.h = 0;
    } else if (state->player1.h + state->player1.paddle_len >= HEIGHT) {
        state->player1.h = HEIGHT - state->player1.paddle_len - 1;
    }
    if (state->player2.h < 0) {
        state->player2.h = 0;
    } else if (state->player2.h + state->player2.paddle_len >= HEIGHT) {
        state->player2.h = HEIGHT - state->player2.paddle_len - 1;
    }

    // 공이 벽에 부딪히면 반사
    if (state->ball.h <= 0 || state->ball.h >= HEIGHT - 1) {
        state->ball.h = state->ball.h <= 0 ? 0 : HEIGHT - 1;
        state->ball.vh = -state->ball.vh;
    }

    // 공이 플레이어 패들에 부딪히면 반사

    // 플레이어 1 패들 충돌 판정
    if (state->ball.w <= state->player1.w + 1) {
        if (state->ball.h >= state->player1.h && state->ball.h <= state->player1.h + state->player1.paddle_len - 1) {
            // 패들에 부딪히면 반사
            state->ball.w = state->player1.w + 1;
            state->ball.vw = -state->ball.vw;

            // TODO: 패들 끝 부분에서는 반사 각도 다르게

            // TODO: 패들 속도에 따라 반사 각도 달라지게

            // 패들 반사속도 관련
            if (state->ball.boost_cnt) {
                state->ball.vh = state->ball.vh / 3;
                state->ball.vw = state->ball.vw / 3;
                state->ball.boost_cnt = 0;
            }

            if (state->player1.paddle_reflect > 100) {
                state->ball.boost_cnt = 1;
            }

            state->ball.vh = state->ball.vh * state->player1.paddle_reflect / 100;
            state->ball.vw = state->ball.vw * state->player1.paddle_reflect / 100;
        }
    }

    // 플레이어 2 패들 충돌 판정
    if (state->ball.w >= state->player2.w - 1) {
        if (state->ball.h >= state->player2.h && state->ball.h <= state->player2.h + state->player2.paddle_len - 1) {
            // 패들에 부딪히면 반사
            state->ball.w = state->player2.w - 1;
            state->ball.vw = -state->ball.vw;

            // TODO: 패들 끝 부분에서는 반사 각도 다르게

            // TODO: 패들 속도에 따라 반사 각도 달라지게

            // 패들 반사속도 관련
            if (state->ball.boost_cnt) {
                state->ball.vh = state->ball.vh / 3;
                state->ball.vw = state->ball.vw / 3;
                state->ball.boost_cnt = 0;
            }

            state->ball.vh = state->ball.vh * state->player2.paddle_reflect / 100;
            state->ball.vw = state->ball.vw * state->player2.paddle_reflect / 100;
        }
    }

    // 공이 좌우 벽에 부딪히면 점수 갱신 후 공 리셋
    // TODO: 먹힌 후 글로벌 딜레이 처리?
    if (state->ball.w <= 0) {
        state->player2.score++;
        reset_ball(state);
    } else if (state->ball.w >= WIDTH - 1) {
        state->player1.score++;
        reset_ball(state);
    }

    // 궁극기 프레임 카운트
    if (state->player1.ult_cnt) state->player1.ult_cnt--;
    if (state->player2.ult_cnt) state->player2.ult_cnt--;

    check_gameover(state);
    return 0;
}

/* render_console()
 * 콘솔 출력
 */
int render_console(GameState *state) {
    printf("\033[H\033[J"); // 화면 클리어
    printf("frame: %d", state->frame);
    printf(" sec: %d\n", state->frame / GAME_FPS);
    printf("ball: [%d, %d](%d, %d)", state->ball.h, state->ball.w, translate_dot(state->ball.h), translate_dot(state->ball.w));
    printf(" player1: [%d, %d](%d, %d)<%d>", state->player1.h, state->player1.w, translate_dot(state->player1.h), translate_dot(state->player1.w), state->player1.ult_cnt);
    printf(" player2: [%d, %d](%d, %d)<%d>\n", state->player2.h, state->player2.w, translate_dot(state->player2.h), translate_dot(state->player2.w), state->player2.ult_cnt);
    printf("score: %d, %d", state->player1.score, state->player2.score);
    printf(" gameover: %d\n", state->gameover);

    // 도트 매트릭스 시뮬레이션
    for (int i = 0; i < DISP_HEIGHT; i++) {
        printf("|");
        for (int j = 0; j < DISP_WIDTH; j++) {
            if (translate_dot(state->ball.h) == i && translate_dot(state->ball.w) == j && state->player1.ult_cnt == 0) {
                printf("●");
            } else if (translate_dot(state->player1.h) <= i && i < translate_dot(state->player1.h) + translate_dot(state->player1.paddle_len) && translate_dot(state->player1.w) == j) {
                printf("■");
            } else if (translate_dot(state->player2.h) <= i && i < translate_dot(state->player2.h) + translate_dot(state->player2.paddle_len) && translate_dot(state->player2.w) == j) {
                printf("■");
            } else {
                printf(" ");
            }
        }
        printf("|\n");
    }
    return 0;
}

int disp_connect = 0;

/* handle_disp()
 * 도트 매트릭스 연결 및 게임 화면 처리 쓰레드
 * 게임 로직 딜레이와 독립적
 */
void *handle_disp(void *arg) {
    GameState *state = (GameState *)arg;
    char msg[9] = {
        0,
    };
    int server_fd;
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(DISP_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int option = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    if (server_fd < 0) {
        perror("Error opening socket for display");
        pthread_exit(NULL);
    }

    if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Error binding socket for display");
        close(server_fd);
        pthread_exit(NULL);
    }

    listen(server_fd, 5);

    // 디스플레이 연결 대기
    int client_fd;
    struct sockaddr_in client_address;
    socklen_t client_address_size = sizeof(client_address);
    client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_address_size);
    if (client_fd < 0) {
        perror("Error accepting display connection");
        close(server_fd);
        pthread_exit(NULL);
    }

    // 디스플레이 연결 완료
    disp_connect = 1;
    printf("Display connected\n");

    // 디스플레이 출력
    while (sock_listen) {
        if (DISPLAY_CONSOLE && ctrl1_connect && ctrl2_connect) render_console(state);
        // 도트 매트릭스 출력 [Ball.h][Ball.x][Player1.h][Player1.x][Player1.padd_len][Player2.h][Player2.x][Player2.padd_len]
        if (state->player1.ult_cnt)
            msg[0] = 99; // 공 안보이도록
        else
            msg[0] = translate_dot(state->ball.h);
        msg[1] = translate_dot(state->ball.w);
        msg[2] = translate_dot(state->player1.h);
        msg[3] = translate_dot(state->player1.w);
        msg[4] = translate_dot(state->player1.paddle_len);
        msg[5] = translate_dot(state->player2.h);
        msg[6] = translate_dot(state->player2.w);
        msg[7] = translate_dot(state->player2.paddle_len);
        write(client_fd, msg, sizeof(msg));
        lcd_clear();
        lcdLoc(LINE1);
        char p1[12] = "PLAYER1: ";
        p1[9] = state->player1.score + '0';
        typeln(p1);
        lcdLoc(LINE2);
        char p2[12] = "PLAYER2: ";
        p2[9] = state->player2.score + '0';
        typeln(p2);
        usleep(DISP_FRAME_TIME_us);
    }

    // 게임 결과 출력
    if (DISPLAY_CONSOLE) render_console(state);
    lcd_clear();
    lcdLoc(LINE1);
    if (state->player1.score > state->player2.score)
        typeln("PLAYER1 WIN!");
    else if (state->player1.score < state->player2.score)
        typeln("PLAYER2 WIN!");
    else
        typeln("!!DRAW!!");
    usleep(1000000 * 5); // 5초 대기

    // 디스플레이 연결 종료
    close(client_fd);
    close(server_fd);
    pthread_exit(NULL);
}

int main(void) {

// LCD 출력시만 사용
#if DISABLE_LCD == 0
    if (wiringPiSetup() == -1) exit(1);
    // LCD init
    lcd_fd = wiringPiI2CSetup(I2C_ADDR);
    lcd_init();
#endif

    GameState state;

    // 쓰레드 종료 플래그 초기화
    sock_listen = 1;

    // 쓰레드 생성
    pthread_t ctrl1_thread, ctrl2_thread, disp_thread;
    if (!DISABLE_SOCK) {
        int ctrl1_port = CTRL1_PORT;
        int ctrl2_port = CTRL2_PORT;
        if (pthread_create(&ctrl1_thread, NULL, handle_ctrl, (void *)&ctrl1_port) < 0) {
            perror("Error creating thread for controller 1");
            exit(1);
        }
        if (pthread_create(&ctrl2_thread, NULL, handle_ctrl, (void *)&ctrl2_port) < 0) {
            perror("Error creating thread for controller 2");
            exit(1);
        }
    }
    if (!DISABLE_DISP) {
        if (pthread_create(&disp_thread, NULL, handle_disp, (void *)&state) < 0) {
            perror("Error creating thread for display");
            exit(1);
        }
    }

    int connect_cnt = 0;
    while (!ctrl1_connect || !ctrl2_connect || !disp_connect) {
        if (DISABLE_SOCK) ctrl1_connect = ctrl2_connect = 1;
        if (DISABLE_DISP) disp_connect = 1;
        printf("\033[H\033[J"); // 화면 클리어

        if (!ctrl1_connect) {
            printf("Waiting for controller 1");
#if DISABLE_LCD == 0
            lcdLoc(LINE1);
            typeln("WAIT CTRL1");
#endif
            for (int i = 0; i < connect_cnt % 6; i++) {
                printf(".");
            }
            printf("\n");
        } else {
            printf("controller 1 connected\n");
#if DISABLE_LCD == 0
            lcdLoc(LINE1);
            typeln("CTRL1 CONN");
#endif
        }

        if (!ctrl2_connect) {
            printf("Waiting for controller 2");
#if DISABLE_LCD == 0
            lcdLoc(LINE2);
            typeln("WAIT CTRL2");
#endif
            for (int i = 0; i < connect_cnt % 6; i++) {
                printf(".");
            }
            printf("\n");
        } else {
            printf("controller 2 connected\n");
#if DISABLE_LCD == 0
            lcdLoc(LINE2);
            typeln("CTRL2 CONN");
#endif
        }
        if (!disp_connect) {
            printf("Waiting for dot matrix");
            for (int i = 0; i < connect_cnt % 6; i++) {
                printf(".");
            }
            printf("\n");
        } else {
            printf("dot matrix connected\n");
        }
        connect_cnt++;
        usleep(500000);
        lcd_clear();
    };

    // 게임 초기화
    init_game(&state);

    // 게임 루프
    while (!state.gameover) {
        update_game(&state);
        if (DISABLE_DISP && DISPLAY_CONSOLE) render_console(&state);
        usleep(FRAME_TIME_us);
    };

    // 소켓 연결 종료
    sock_listen = 0;

    if (!DISABLE_SOCK) {
        // 쓰레드 종료 대기
        pthread_join(ctrl1_thread, NULL);
        pthread_join(ctrl2_thread, NULL);
    }
    if (!DISABLE_DISP) {
        pthread_join(disp_thread, NULL);
    }

    return 0;
}
