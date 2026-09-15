/*
 * app.h - 메인 애플리케이션 로직(입력 폴링, 레이아웃/트리거/그리기 오케스트레이션).
 *   main.c는 하드웨어 초기화 + app_init()/app_run_frame() 호출만 하도록 최대한 얇게 유지하고,
 *   UI 상태/파형 인스턴스/트리거 오프셋 같은 실제 애플리케이션 상태는 전부 이 모듈 안에 있다.
 */
#ifndef APP_H
#define APP_H

// UI 상태 초기화 + 초기 화면 그리기. main()에서 하드웨어 init(spi/hc165/lcd/adc/millis/calib)
// 다 끝난 뒤 한 번만 호출.
void app_init(void);

// 메인루프 한 바퀴: 입력 폴링 + (프레임 준비됐고 조건 맞으면) 화면 갱신 + 지연된 UI
// 변경사항 반영. main()의 while(1) 안에서 계속 불러주면 됨.
void app_run_frame(void);

#endif
