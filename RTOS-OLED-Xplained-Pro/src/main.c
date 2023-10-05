#include <asf.h>
#include "conf_board.h"

#include "gfx_mono_ug_2832hsweg04.h"
#include "gfx_mono_text.h"
#include "sysfont.h"

#define BUT1_PIO PIOD  // Botão 1
#define BUT1_PIO_ID ID_PIOD
#define BUT1_PIO_IDX	28
#define BUT1_PIO_IDX_MASK (1u << BUT1_PIO_IDX)

#define BUT2_PIO PIOC // Botão 2
#define BUT2_PIO_ID ID_PIOC
#define BUT2_PIO_IDX 31
#define BUT2_PIO_IDX_MASK (1u << BUT2_PIO_IDX)

#define BUT3_PIO PIOA // Botão 3
#define BUT3_PIO_ID ID_PIOA
#define BUT3_PIO_IDX 19
#define BUT3_PIO_IDX_MASK (1u << BUT3_PIO_IDX)

#define PIN1_PIO PIOD// Pino 1
#define PIN1_PIO_ID ID_PIOD
#define PIN1_PIO_IDX 30
#define PIN1_PIO_IDX_MASK (1u << PIN1_PIO_IDX)

#define PIN2_PIO PIOA// Pino 2
#define PIN2_PIO_ID ID_PIOA
#define PIN2_PIO_IDX 6
#define PIN2_PIO_IDX_MASK (1u << PIN2_PIO_IDX)

#define PIN3_PIO PIOC// Pino 3
#define PIN3_PIO_ID ID_PIOC
#define PIN3_PIO_IDX 19
#define PIN3_PIO_IDX_MASK (1u << PIN3_PIO_IDX)

#define PIN4_PIO PIOA// Pino 4
#define PIN4_PIO_ID ID_PIOA
#define PIN4_PIO_IDX 2
#define PIN4_PIO_IDX_MASK (1u << PIN4_PIO_IDX)


/** RTOS  */
#define TASK_OLED_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_OLED_STACK_PRIORITY            (tskIDLE_PRIORITY)

#define STEP 0.17578125

QueueHandle_t xQueueModo;
QueueHandle_t xQueueSteps;
SemaphoreHandle_t xSemaphoreRTT;

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

/** prototypes */
void but_callback(void);
static void BUT_init(void);

/************************************************************************/
/* RTOS application funcs                                               */
/************************************************************************/
static void RTT_init(float freqPrescale, uint32_t IrqNPulses, uint32_t rttIRQSource);

void RTT_Handler(void);

void pin1(int state);

void pin2(int state);

void pin3(int state);

void pin4(int state);

void PINS_init(void);


extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

void but1_callback(void){
	int modo = 180;
	xQueueSendFromISR(xQueueModo, &modo, 0);

}

void but2_callback(void){
	int modo = 90;
	xQueueSendFromISR(xQueueModo, &modo, 0);
}

void but3_callback(void){
	int modo = 45;
	xQueueSendFromISR(xQueueModo, &modo, 0);
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_modo(void *pvParameters) {
	gfx_mono_ssd1306_init();
	BUT_init();
	int modo;
	int steps;
	char modo_str[128];
	for (;;)  {
		if(xQueueReceive(xQueueModo, &modo, (TickType_t) 0)){
			steps = modo/STEP;
			xQueueSend(xQueueSteps, &steps, (TickType_t) 0);
			sprintf(modo_str, "%d graus", modo);
			gfx_mono_draw_string(modo_str, 50,16, &sysfont);
		}
		
	}
}

static void task_motor(void *pvParameters) {
	gfx_mono_ssd1306_init();
	PINS_init();
	int steps;
	int state = 1;
	int ticks;
	int freq = 1000;
	float time;
	RTT_init(freq, 10, RTT_MR_ALMIEN);
	
	for (;;)  {
		if (xQueueReceive(xQueueSteps, &steps, (TickType_t) 0)) {
			
			
			for(int i=0; i < steps; i++) {
				
				pin1(state & 1);
				pin2(state & 2);
				pin3(state & 4);
				pin4(state & 8);

				if (state == 8){
					state = 1;
				}
				else{
					state = state << 1;
				}
 				xSemaphoreTake(xSemaphoreRTT, 10000);
 				
			}
		}

	}
}

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/
void RTT_Handler(void) {
  uint32_t ul_status;
  ul_status = rtt_get_status(RTT);

  /* IRQ due to Alarm */
  if ((ul_status & RTT_SR_ALMS) == RTT_SR_ALMS) {
		RTT_init(1000, 10, RTT_MR_ALMIEN);
    	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xSemaphoreGiveFromISR(xSemaphoreRTT, &xHigherPriorityTaskWoken);
   }  
}

static void RTT_init(float freqPrescale, uint32_t IrqNPulses, uint32_t rttIRQSource) {

  uint16_t pllPreScale = (int) (((float) 32768) / freqPrescale);
	
  rtt_sel_source(RTT, false);
  rtt_init(RTT, pllPreScale);
  
  if (rttIRQSource & RTT_MR_ALMIEN) {
	uint32_t ul_previous_time;
  	ul_previous_time = rtt_read_timer_value(RTT);
  	while (ul_previous_time == rtt_read_timer_value(RTT));
  	rtt_write_alarm_time(RTT, IrqNPulses+ul_previous_time);
  }

  /* config NVIC */
  NVIC_DisableIRQ(RTT_IRQn);
  NVIC_ClearPendingIRQ(RTT_IRQn);
  NVIC_SetPriority(RTT_IRQn, 4);
  NVIC_EnableIRQ(RTT_IRQn);

  /* Enable RTT interrupt */
  if (rttIRQSource & (RTT_MR_RTTINCIEN | RTT_MR_ALMIEN))
	rtt_enable_interrupt(RTT, rttIRQSource);
  else
	rtt_disable_interrupt(RTT, RTT_MR_RTTINCIEN | RTT_MR_ALMIEN);
		  
}

void pin1(int state) {
	if (state) {
		pio_set(PIN1_PIO, PIN1_PIO_IDX_MASK);
	} else{
		pio_clear(PIN1_PIO, PIN1_PIO_IDX_MASK);
	}
}

void pin2(int state) {
	if (state) {
		pio_set(PIN2_PIO, PIN2_PIO_IDX_MASK);
	} else{
		pio_clear(PIN2_PIO, PIN2_PIO_IDX_MASK);
	}
}

void pin3(int state) {
	if (state) {
		pio_set(PIN3_PIO, PIN3_PIO_IDX_MASK);
	} else{
		pio_clear(PIN3_PIO, PIN3_PIO_IDX_MASK);
	}
}

void pin4(int state) {
	if (state) {
		pio_set(PIN4_PIO, PIN4_PIO_IDX_MASK);
	} else{
		pio_clear(PIN4_PIO, PIN4_PIO_IDX_MASK);
	}
}
static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = CONF_UART_BAUDRATE,
		.charlength = CONF_UART_CHAR_LENGTH,
		.paritytype = CONF_UART_PARITY,
		.stopbits = CONF_UART_STOP_BITS,
	};

	/* Configure console UART. */
	stdio_serial_init(CONF_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}

void BUT_init(void){

	// Inicializa clock do periférico PIO responsavel pelo botao
	pmc_enable_periph_clk(BUT1_PIO_ID);
    pmc_enable_periph_clk(BUT2_PIO_ID);
	pmc_enable_periph_clk(BUT3_PIO_ID);

	// Pull-up e debounce
	pio_configure(BUT1_PIO, PIO_INPUT, BUT1_PIO_IDX_MASK, PIO_PULLUP | PIO_DEBOUNCE);
	pio_set_debounce_filter(BUT1_PIO, BUT1_PIO_IDX_MASK, 60);

    pio_configure(BUT2_PIO, PIO_INPUT, BUT2_PIO_IDX_MASK, PIO_PULLUP | PIO_DEBOUNCE);
	pio_set_debounce_filter(BUT2_PIO, BUT2_PIO_IDX_MASK, 60);

	pio_configure(BUT3_PIO, PIO_INPUT, BUT3_PIO_IDX_MASK, PIO_PULLUP | PIO_DEBOUNCE);
	pio_set_debounce_filter(BUT3_PIO, BUT3_PIO_IDX_MASK, 60);

	// Pio handler
	pio_handler_set(BUT1_PIO, BUT1_PIO_ID, BUT1_PIO_IDX_MASK, PIO_IT_FALL_EDGE, but1_callback);

    pio_handler_set(BUT2_PIO, BUT2_PIO_ID, BUT2_PIO_IDX_MASK, PIO_IT_FALL_EDGE, but2_callback);

	pio_handler_set(BUT3_PIO, BUT3_PIO_ID, BUT3_PIO_IDX_MASK, PIO_IT_FALL_EDGE, but3_callback);

	// Ativa interrupção e limpa primeira IRQ gerada na ativacao
	pio_enable_interrupt(BUT1_PIO, BUT1_PIO_IDX_MASK);
	pio_get_interrupt_status(BUT1_PIO);

    pio_enable_interrupt(BUT2_PIO, BUT2_PIO_IDX_MASK);
	pio_get_interrupt_status(BUT2_PIO);

	pio_enable_interrupt(BUT3_PIO, BUT3_PIO_IDX_MASK);
	pio_get_interrupt_status(BUT3_PIO);

	// Configura NVIC para receber interrupcoes do PIO do botao
	// com prioridade 4 (quanto mais próximo de 0 maior)

	NVIC_EnableIRQ(BUT1_PIO_ID);
	NVIC_SetPriority(BUT1_PIO_ID, 4);

    NVIC_EnableIRQ(BUT2_PIO_ID);
	NVIC_SetPriority(BUT2_PIO_ID, 4);

	NVIC_EnableIRQ(BUT3_PIO_ID);
	NVIC_SetPriority(BUT3_PIO_ID, 4);
	
}

void PINS_init(void){
	pmc_enable_periph_clk(PIN1_PIO_ID);
	pio_set_output(PIN1_PIO, PIN1_PIO_IDX_MASK, 0, 0, 0);

	pmc_enable_periph_clk(PIN2_PIO_ID);
	pio_set_output(PIN2_PIO, PIN2_PIO_IDX_MASK, 0, 0, 0);

	pmc_enable_periph_clk(PIN3_PIO_ID);
	pio_set_output(PIN3_PIO, PIN3_PIO_IDX_MASK, 0, 0, 0);

	pmc_enable_periph_clk(PIN4_PIO_ID);
	pio_set_output(PIN4_PIO, PIN4_PIO_IDX_MASK, 0, 0, 0);
}


/************************************************************************/
/* main                                                                 */
/************************************************************************/


int main(void) {
	/* Initialize the SAM system */
	sysclk_init();
	board_init();

	/* Initialize the console uart */
	configure_console();
	
	/* Creating queues */
	configure_console();
	xQueueModo = xQueueCreate(32, sizeof(int));
  	if (xQueueModo == NULL)
    	printf("falha em criar a queue xQueueModo \n");

	xQueueSteps = xQueueCreate(32, sizeof(int));
  	if (xQueueSteps == NULL)
    	printf("falha em criar a queue xQueueSteps \n");
	
	/* Creating semaphores*/

	xSemaphoreRTT = xSemaphoreCreateBinary();
	if (xSemaphoreRTT == NULL)
		printf("falha em criar o semaforo xSemaphoreRTT \n");

	/* Create task */
	if (xTaskCreate(task_modo, "modo", TASK_OLED_STACK_SIZE, NULL, TASK_OLED_STACK_PRIORITY, NULL) != pdPASS) {
	  printf("Failed to create modo task\r\n");
	}

	if (xTaskCreate(task_motor, "motor", TASK_OLED_STACK_SIZE, NULL, TASK_OLED_STACK_PRIORITY, NULL) != pdPASS) {
	  printf("Failed to create motor task\r\n");
	}

	/* Start the scheduler. */
	vTaskStartScheduler();

  /* RTOS n�o deve chegar aqui !! */
	while(1){}

	/* Will only get here if there was insufficient memory to create the idle task. */
	return 0;
}
