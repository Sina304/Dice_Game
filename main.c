#include "MKL05Z4.h"
#include "ADC.h"
#include "frdm_bsp.h"
#include "lcd1602.h"
#include "i2c.h"
#include "klaw.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define A 1664525  			//ziarno
#define C 1013904223		//dodawana liczba
#define M (1LL << 32)   // modulo rowne 2^32 
#define N 500						// rozmiar tablicy gra pozwala na rozegranie 16 gier (jedna gra wymaga 5*3*2=30 elementow tablicy) 500/30 = 16 z reszta 20
#define ROUNDS 3				// iloœæ rund 


volatile uint8_t S1_press=0;
volatile uint8_t S2_press=0;	// "1" - klawisz zosta³ wciœniêty "0" - klawisz "skonsumowany"
volatile uint8_t S3_press=0;
volatile uint8_t S4_press=0;

uint8_t rotation=0;					// zmiana koœci któr¹ operujemy
uint8_t take=0;							// Liczba wciœniêæ klawisza S3
uint8_t S4_nr=0;						// Kolejne "serie" koœci

uint8_t dice_1=0;			//zmienna odpowiadaj¹ca za to czy operujemy dana koœcia czy nie(czy chcemy ja zmienic czy nie)
uint8_t	dice_2=0;
uint8_t	dice_3=0;
uint8_t	dice_4=0;
uint8_t dice_5=0;

uint8_t d1fr=1;  	// dice x from round - poslyzy za to z ktorej rundy ma byc teraz kosc
uint8_t d2fr=1;		// zmienne stworzone w celu naprawy bledu wystepujego w S3
uint8_t d3fr=1;
uint8_t d4fr=1;
uint8_t d5fr=1;

uint8_t r=0;							// r od round - odpowiada za to ktora runda jest
volatile uint8_t over=0;	// over=0 gra trwa | over = 1 gra zakoñczona (trwa podsumowanie) | over = 2 gra gotowa do ponownego rozpoczêcia (odblokowany klawisz S1)

uint8_t dice_1_ack = 0;		// zmienne u¿ywane w prawid³owego funkcjonowania funkcji blink
uint8_t dice_2_ack = 0;
uint8_t dice_3_ack = 0;
uint8_t dice_4_ack = 0;
uint8_t dice_5_ack = 0;

volatile game_on=0;			// kolejna zmienna odpowiadajaca za to czy gra trwa
uint8_t game=0;					// zmienna odpowiadajaca za to ktora gre gramy

char display[]={0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20};
uint8_t numbers[N];			//tablica wygenerowanych liczb pseudolowych (narazie pusta, wype³niana po wywo³aniu funkcji gen())

volatile uint8_t starter=0; // zmienna dzieki ktorej po wcisnieciu S1 wchodzimy do petli glownej a tym samym zaczynamy pierwsza gre

uint8_t dices[5]; // koœci bota

volatile float adc_volt_coeff = ((float)(((float)2.91) / 4095) );			// Wspó³czynnik korekcji wyniku, w stosunku do napiêcia referencyjnego przetwornika
volatile uint8_t wynik_ok=0;
volatile uint16_t temp;
volatile float	wynik;
uint32_t seed=0;
uint8_t no_more=0;

void ADC0_IRQHandler()
{	
	temp = ADC0->R[0];	// Odczyt danej i skasowanie flagi COCO
	if(!wynik_ok && !no_more)				// SprawdŸ, czy wynik skonsumowany przez pêtlê g³ówn¹
	{
		wynik = temp;			// Wyœlij now¹ dan¹ do pêtli g³ównej
		wynik_ok=1;
		no_more=1;
	}
}

void gen(uint32_t s){		// generator liczb pseudolowych
	uint32_t x = s;
	uint8_t y = 0;
	for (uint16_t n = 0; n < N; n++) {
		x = (A * x + C) % M;
		y =(x % 6) + 1;
		numbers[n] = y;
	}
}

void first_throw(){	// wyœwietla na lcd jak rzuciæ koœcmi
	LCD1602_ClearAll();
	LCD1602_SetCursor(0,0);
	LCD1602_Print("Aby wykonac rzut");
	LCD1602_SetCursor(2,1);
	LCD1602_Print("nacisnij S4");
}

void wanna_play_again(){	// pyta czy chcemy zagraæ nastêpn¹ gre (wciœniêcie S1 = nastepna gra)
	LCD1602_ClearAll();					
	LCD1602_SetCursor(0,0);			
	LCD1602_Print("Nacisnij S1 aby");
	LCD1602_SetCursor(0,1);
	LCD1602_Print("zagrac ponownie");
	while(over);
	game++;
	first_throw();
}

void bot(){		// bot rozgrywa swoj¹ (trochê jak w bj - najpierw gra gracz potem krupier(bot)
	S4_nr++;
	for(uint8_t n=0; n<5; n++){
		dices[n] = numbers[S4_nr*5-5+n];
	}
	LCD1602_SetCursor(10,1);			
	LCD1602_Print("<- BOT");
	LCD1602_SetCursor(0,1);
	sprintf(display, "%d %d %d %d %d", dices[0], dices[1], dices[2], dices[3], dices[4]);
	LCD1602_Print(display);
	for(uint8_t k=0; k<2; k++){
		DELAY(30000)
		S4_nr++;
		for(uint8_t n=0; n<5; n++){
			if(dices[n]<=3){
				dices[n] = numbers[S4_nr*5-5+n];
				LCD1602_SetCursor(0,1);
				sprintf(display, "%d %d %d %d %d", dices[0], dices[1], dices[2], dices[3], dices[4]);
				LCD1602_Print(display);
			}
		}
	}
	DELAY(30000)
	LCD1602_SetCursor(0,1);
	LCD1602_Print("                ");
	LCD1602_SetCursor(0,1);
	sprintf(display, "Suma enemy: %d", dices[0]+dices[1]+dices[2]+dices[3]+dices[4]);
	LCD1602_Print(display);
}

void PORTA_IRQHandler(void){	// Podprogram obs³ugi przerwania od klawiszy S2, S3 i S4
	uint32_t buf;
	buf=PORTA->ISFR & (S1_MASK | S2_MASK | S3_MASK | S4_MASK);

	switch(buf)
	{	
		case S1_MASK:	DELAY(100)
									if(!(PTA->PDIR&S1_MASK))		// Minimalizacja drgañ zestyków
									{
										DELAY(100)
										if(!(PTA->PDIR&S1_MASK))	// Minimalizacja drgañ zestyków (c.d.)
										{
											if(!S1_press && !game_on)
											{
												game_on = 1;
												S1_press=1;
												if(!starter){
													starter = 1;
												}
												if(over==2){
													over=0;
												}
											}
										}
									}
									break;
		case S2_MASK:	DELAY(100)
									if(!(PTA->PDIR&S2_MASK))		// Minimalizacja drgañ zestyków
									{
										DELAY(100)
										if(!(PTA->PDIR&S2_MASK))	// Minimalizacja drgañ zestyków (c.d.)
										{
											if(!S2_press && !over && r!=0)
											{
												if(rotation>=1 && rotation<=7){  // zmiana pozycji strza³ki (1,3,5,7,9)
													rotation+=2;
												}
												else{
													rotation=1;
												}
												S2_press=1;
											}
										}
									}
									break;
		case S3_MASK:	DELAY(100)
									if(!(PTA->PDIR&S3_MASK))		// Minimalizacja drgañ zestyków
									{
										DELAY(100)
										if(!(PTA->PDIR&S3_MASK))	// Minimalizacja drgañ zestyków (c.d.)
										{
											if(!S3_press && !over && r!=0)
											{
												switch(rotation){
													case 1:
														dice_1 = dice_1 ^ 1;
														break;
													case 3:
														dice_2 = dice_2 ^ 1;
														break;
													case 5:
														dice_3 = dice_3 ^ 1;
														break;
													case 7:
														dice_4 = dice_4 ^ 1;
														break;
													case 9:
														dice_5 = dice_5 ^ 1;
														break;
													default: break;
												}
												S3_press=1;
											}
										}
									}
									break;
									case S4_MASK:	DELAY(100)
									if(!(PTA->PDIR&S4_MASK))		// Minimalizacja drgañ zestyków
									{
										DELAY(100)
										if(!(PTA->PDIR&S4_MASK))	// Minimalizacja drgañ zestyków (c.d.)
										{
											if(!S4_press && !over)
											{
												S4_press=1;
												r++;
												S4_nr++;
												if(r==1){
													d1fr = S4_nr;
													d2fr = S4_nr;
													d3fr = S4_nr;
													d4fr = S4_nr;
													d5fr = S4_nr;
												}
												else if(r<=ROUNDS){
													if(dice_1){
														d1fr = S4_nr;
													}
													if(dice_2){
														d2fr = S4_nr;
													}
													if(dice_3){
														d3fr = S4_nr;
													}
													if(dice_4){
														d4fr = S4_nr;
													}
													if(dice_5){
														d5fr = S4_nr;
													}
												}
												else{
													r=0;
													over = 1;
												}
											}
										}
									}
									
									NVIC_ClearPendingIRQ(PORTA_IRQn);
									break;
		default:			break;
	}	
	PORTA->ISFR |=  S1_MASK | S2_MASK | S3_MASK | S4_MASK;	// Kasowanie wszystkich bitów ISF
	NVIC_ClearPendingIRQ(PORTA_IRQn);
}

void blink(uint8_t *dice_ack, uint8_t number, uint8_t dfr){  // (który ack oraz która koœæ) funkcja odpowiadajaca za to zeby wybrane koœci miga³y
	if(*dice_ack){
		LCD1602_SetCursor(number*2-2,0);
		LCD1602_Print(" ");
		*dice_ack = 0;
	}
	else{
		LCD1602_SetCursor(number*2-2,0);
		sprintf(display, "%d", numbers[dfr*5-6+number]);
		LCD1602_Print(display);
		*dice_ack = 1;
	}
}

void show_dice(uint8_t x){ // funkcja odpowiedzialna za wyœwietlanie poszczególnych koœci
	LCD1602_SetCursor(x*2-2,0);
	if(x==1){
		sprintf(display, "%d", numbers[d1fr*5-6+x]);
	}
	if(x==2){
		sprintf(display, "%d", numbers[d2fr*5-6+x]);
	}
	if(x==3){
		sprintf(display, "%d", numbers[d3fr*5-6+x]);
	}
	if(x==4){
		sprintf(display, "%d", numbers[d4fr*5-6+x]);
	}
	if(x==5){
		sprintf(display, "%d", numbers[d5fr*5-6+x]);
	}
	LCD1602_Print(display);
}

void check(){ // funkcja sprawdzaj¹ca kto wygra³
	if((dices[0]+dices[1]+dices[2]+dices[3]+dices[4])>(numbers[d1fr*5-5]+numbers[d2fr*5-4]+numbers[d3fr*5-3]+numbers[d4fr*5-2]+numbers[d5fr*5-1])){
		LCD1602_ClearAll();					
		LCD1602_SetCursor(3,0);			
		LCD1602_Print("Przegrales");
	}
	else if((dices[0]+dices[1]+dices[2]+dices[3]+dices[4])<(numbers[d1fr*5-5]+numbers[d2fr*5-4]+numbers[d3fr*5-3]+numbers[d4fr*5-2]+numbers[d5fr*5-1])){
		LCD1602_ClearAll();					
		LCD1602_SetCursor(4,0);			
		LCD1602_Print("Wygrales");
	}
	else{
		LCD1602_ClearAll();					
		LCD1602_SetCursor(0,0);			
		LCD1602_Print("Remis");
	}
}


int main(void){
	uint8_t	kal_error;
	LCD1602_Init();		 					// Inicjalizacja LCD
	LCD1602_Backlight(TRUE);  	// W³¹czenie podœwietlenia
	kal_error=ADC_Init();				// Inicjalizacja i kalibracja przetwornika A/C
	if(kal_error)
	{	
		while(1);									// Klaibracja siê nie powiod³a
	}
	ADC0->SC1[0] = ADC_SC1_AIEN_MASK | ADC_SC1_ADCH(8);		// Pierwsze wyzwolenie przetwornika ADC0 w kanale 8 i odblokowanie przerwania
	uint8_t n=0;								//n od petli do migania
	Klaw_Init();
	Klaw_S2_4_Int();
	LCD1602_ClearAll();					// Wyczyœæ ekran
	LCD1602_SetCursor(0,0);			// Wyœwietl stan pocz¹tkowy pola dotykowego
	LCD1602_Print("Wcisnij przycisk");
	LCD1602_SetCursor(0,1);			// Ustaw kursor na pocz¹tku drugiej linii
	LCD1602_Print("S1 aby rozpoczac");
	
	while(starter == 0){ //start gry dopiero po wcisnieciu S1
		continue;
	}
	
	S1_press = 0;
	S2_press = 0;
	S3_press = 0;
	S4_press = 0;
	
	first_throw();
	
	while(1) {
		if(wynik_ok)
		{
			wynik = wynik*adc_volt_coeff;		// Dostosowanie wyniku do zakresu napiêciowego
			wynik = wynik*10000;
			seed = (uint32_t) wynik;
			gen(seed);								//generuje liczby pseudolosowe
			wynik_ok=0;
		}
		if(S1_press){
			S1_press = 0;
		}
		if(S2_press && r>0){
			S2_press = 0;
			LCD1602_SetCursor(0,1);
			LCD1602_Print("                ");
			LCD1602_SetCursor(rotation-1,1);
			LCD1602_Print("^");
		}
		if(S3_press && r>0){
			S3_press = 0;
			dice_1_ack = 0;
			dice_2_ack = 0;
			dice_3_ack = 0;
			dice_4_ack = 0;
			dice_5_ack = 0;
			if(!dice_1){
				LCD1602_SetCursor(0,0);
				sprintf(display, "%d", numbers[d1fr*5-5]);
				LCD1602_Print(display);
			}
			if(!dice_2){
				LCD1602_SetCursor(2,0);
				sprintf(display, "%d", numbers[d2fr*5-4]);
				LCD1602_Print(display);
			}
			if(!dice_3){
				LCD1602_SetCursor(4,0);
				sprintf(display, "%d", numbers[d3fr*5-3]);
				LCD1602_Print(display);
			}
			if(!dice_4){
				LCD1602_SetCursor(6,0);
				sprintf(display, "%d", numbers[d4fr*5-2]);
				LCD1602_Print(display);
			}
			if(!dice_5){
				LCD1602_SetCursor(8,0);
				sprintf(display, "%d", numbers[d5fr*5-1]);
				LCD1602_Print(display);
			}
		}
		if(S4_press){
			S4_press = 0;
			if(r==0){
				LCD1602_ClearAll();
				LCD1602_SetCursor(0,0);
				sprintf(display, "Twoja suma: %d", numbers[d1fr*5-5]+numbers[d2fr*5-4]+numbers[d3fr*5-3]+numbers[d4fr*5-2]+numbers[d5fr*5-1]);
				LCD1602_Print(display);
				dice_1=0;
				dice_2=0;
				dice_3=0;
				dice_4=0;
				dice_5=0;
			}
			else{
				if(starter==1){
					LCD1602_ClearAll();
					sprintf(display, "%d %d %d %d %d", numbers[d1fr*5-5], numbers[d2fr*5-4], numbers[d3fr*5-3], numbers[d4fr*5-2], numbers[d5fr*5-1]);
					LCD1602_Print(display);
					starter = 0;
				}
				else{
					if (dice_1){
						show_dice(1);
					}
					if (dice_2){
						show_dice(2);
					}
					if (dice_3){
						show_dice(3);
					}
					if (dice_4){
						show_dice(4);
					}
					if (dice_5){
						show_dice(5);
					}
				}
			}
			if(over){ 				//wjedzie na over 1 (dla 2 wsm tez)
				DELAY(20000)
				bot();
				DELAY(40000)
				check();
				DELAY(30000)
				over++;					// over = 2
				game_on = 0;
				wanna_play_again();
			}
			// sprintf(display, "%d %d %d %d %d", numbers[S4_nr*5-5], numbers[S4_nr*5-4], numbers[S4_nr*5-3], numbers[S4_nr*5-2], numbers[S4_nr*5-1]); // to trzeba zamienic na 5 oddzielnych
		}
		DELAY(100)
		n++;
		if(n==50){
			n=0;
			if (dice_1){
				blink(&dice_1_ack, 1, d1fr);
			}
			if (dice_2){
				blink(&dice_2_ack, 2, d2fr);
			}
			if (dice_3){
				blink(&dice_3_ack, 3, d3fr);
			}
			if (dice_4){
				blink(&dice_4_ack, 4, d4fr);
			}
			if (dice_5){
				blink(&dice_5_ack, 5, d5fr);
			}
		}
	}
}
