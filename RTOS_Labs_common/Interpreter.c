// *************Interpreter.c**************
// Students implement this as part of EE445M/EE380L.12 Lab 1,2,3,4 
// High-level OS user interface
// 
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano 1/18/20, valvano@mail.utexas.edu
#include <stdint.h>
#include <string.h> 
#include <stdio.h>
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../RTOS_Labs_common/esp8266.h"
#include "../inc/ADCT0ATrigger.h"
#include "../inc/ADCSWTrigger.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/eDisk.h"
#include "../RTOS_Labs_common/eFile.h"
#include "../RTOS_Labs_common/ADC.h"
#include "../RTOS_Lab5_ProcessLoader\loader.h"


extern int32_t MaxJitter;             // largest time jitter between interrupts in usec
extern uint32_t const JitterSize;
extern uint32_t JitterHistogram[];

extern int serverClientStatus;

#define PF1  (*((volatile uint32_t *)0x40025008))
#define PF2  (*((volatile uint32_t *)0x40025010))
#define PF3  (*((volatile uint32_t *)0x40025020))


int telnetServerID = -1;

void Interpreter_OutString(char *s) {
	if(OS_Id() == telnetServerID) {
		if(!ESP8266_Send(s)){ 
			// Error handling, close and kill 
		}
	}else{
		UART_OutString(s);
	}
}

void Interpreter_InString(char *bufPt, uint16_t max){
	if(OS_Id() == telnetServerID) {
		if(!ESP8266_Receive(bufPt, max)){ 
			// Error handling, close and kill 
		}
	}else{
		UART_InString(bufPt, max);
	}
}



// Print jitter histogram
void Jitter(int32_t MaxJitter, uint32_t const JitterSize, uint32_t JitterHistogram[]){
  // write this for Lab 3 (the latest)
	Interpreter_OutString("MaxJitter: ");
	UART_OutSDec(MaxJitter);
	UART_OutChar('\n');
	
	Interpreter_OutString("JitterHistogram: ");
	UART_OutChar('\n');
	
	for(int i = 0; i < JitterSize; i++){
		UART_OutSDec(i);
		Interpreter_OutString(" : ");
		UART_OutUDec(JitterHistogram[i]);
		UART_OutChar('\n');
	}
}


// Format the disk
void FormatDisk(){
	// from lab 4
	//eFile_FormatDisk();
	// from lab 5
	Interpreter_OutString("\n\r");
	if(eFile_Format()){
		Interpreter_OutString("can't format");
	}else{
		Interpreter_OutString("formatted");
	}
	Interpreter_OutString("\n\r");
}

// Print eFile directory
void PrintDirectory(){
	// from lab 4
	//eFile_PrintDirectory();
}

// Print an eFile file
void PrintFile(char* fileName){
	// from lab 4
	//eFile_PrintFile(fileName);
}

// Delete an eFile file
void DeleteFile(char* fileName){
	// from lab 4
	//eFile_DeleteFile(fileName);
}

// Create an eFile file
void CreateFile(char* fileName){
	// from lab 4
	//eFile_CreateFile(fileName);
}

// Write to an eFile file
void WriteToFile(char* fileName, char* string){
	// from lab 4
	//eFile_WriteToFile(fileName, string);
}

// Unmount current file system
void UnmountFS(void){
	// from lab 4
	//eFile_UnmountFS();
	
	Interpreter_OutString("\n\r");
	if(eFile_Unmount()){
		Interpreter_OutString("can't unmount");
	}else{
		Interpreter_OutString("unmounted");
	}
	Interpreter_OutString("\n\r");
	
}

void MountFs(void){
	// lab 5
	Interpreter_OutString("\n\r");
	if(eFile_Mount()){
		Interpreter_OutString("can't mount");
	}else{
		Interpreter_OutString("mounted");
	}
	Interpreter_OutString("\n\r");
}

static const ELFSymbol_t symtab[] = {
 { "ST7735_Message", ST7735_Message } // address of ST7735_Message
}; 

// Load User Program
void LoadProg(char* fileName, int32_t fileNameLength){
	Interpreter_OutString("\n\r");
	ELFEnv_t env = { symtab, 1 }; // symbol table with one entry
  if(!exec_elf("User.axf", &env)) {
		Interpreter_OutString("Load Error");
	}
}

void interpreterLED(int ledNum){
	Interpreter_OutString("\n\r");
	if(ledNum == 1){
		PF1 ^= 0x02;
		Interpreter_OutString("toggled PF1");
	}else if(ledNum == 2){
		PF2 ^= 0x04;
		Interpreter_OutString("toggled PF2");
	}else if(ledNum == 3){
		PF3 ^= 0x08;
		Interpreter_OutString("toggled PF3");
	}
	Interpreter_OutString("\n\r");
}

int exitTelnetSession(void){
	Interpreter_OutString("\n\r");
	if(OS_Id() == telnetServerID){
		Interpreter_OutString("exiting telnet session");
		Interpreter_OutString("\n\r");
		return 1;
	}else{
		Interpreter_OutString("this terminal is main prog");
		Interpreter_OutString("\n\r");
		return 0;
	}
}

void selectServerClient(void){
	Interpreter_OutString("\n\r");
	Interpreter_OutString("type 1 to be a server, and 0 to be a client: ");
	char serverClientSelectionBuffer[2];
	Interpreter_InString(serverClientSelectionBuffer, 1);
	if(serverClientSelectionBuffer[0] == '1'){
		serverClientStatus = 1;
		Interpreter_OutString("\n\r");
		Interpreter_OutString("configured to be a server");
	}else if(serverClientSelectionBuffer[0] == '0'){
		serverClientStatus = 0;
		Interpreter_OutString("\n\r");
		Interpreter_OutString("configured to be a client");
	}else{
		Interpreter_OutString("invalid selection");
	}
	Interpreter_OutString("\n\r");
}



// 34 because, message length is 16, '|' delimiter is 1, command is 10 (for lcd_xyz), negative -9999 is 5, 1 extra to check message overflow, null char = 34
#define inputMessageBufferLength	34
#define commandLength							8
#define eFileNameLength						8
#define eFileWriteLength					100
#define userProgNameLength				10
char inputMessageBuffer[inputMessageBufferLength];
char eFileNameBuffer[eFileNameLength];
char eFileWriteBuffer[eFileWriteLength];
char userProgNameBuffer[userProgNameLength];

char* terminalWelcomeString = "Welcome to 445M! Type 'hlp_cmd' to get started \n";
char* helpString = "hlp_cmd";
char* helpStringResult = "\n"
											   "\n"
												 "hlp_cmd\tprints out possible commands"
												 "\n"
												 "lcd_top\t<line> <string>|<num>"
												 "\n"
												 "lcd_bot\t<line> <string>|<num>"
												 "\n"
												 "adc_inp\tprints out ADC0 on channel 0(PE3) to terminal screen"
												 "\n"
												 "get_tme\tprints out uptime to terminal screen"
												 "\n"
												 "clr_tme\tclears time"
												 "\n"
												 "jit_his\tprints out jitter histogram and max jitter"
												 "\n"
												 "prt_dir\tprints out eFile directory"
												 "\n"
												 "prt_fil\tprints out eFile file"
												 "\n"
												 "del_fil\tdelete specified eFile file"
												 "\n"
												 "fmt_dsk\tformat eDisk"
												 "\n"
												 "crt_fil\tcreate file with specified name eDisk"
												 "\n"
												 "wrt_fil\tcreate file with specified name eDisk and string of 8 characters"
												 "\n"
												 "umt_fil\tunmount file"
												 "\n"
												 "lod_prg\tload prog with specified name"
												 "\n"
												 "mnt_fls\tmount file"
												 "\n"
												 "led_001\ttoggle on PF1"
												 "\n"
												 "led_010\ttoggle on PF2"
												 "\n"
												 "led_011\ttoggle on PF3"
												 "\n"
												 "ext_tnt\texit telnet session"
												 "\n"
												 "sel_scl\tchoose to be server or client"
												 "\n";
char* invalidCommandResult = "\ninvalid command\n";
char* invalidNumArgsResult = "\n<line> must be between 0-6\n";

static void clearInputMessageBuffer(){
	for(int i = 0; i < inputMessageBufferLength; i++){
		inputMessageBuffer[i] = '\0'; 
	}
}

// *********** Command line interpreter (shell) ************
void Interpreter(void){ 
  // write this
	Interpreter_OutString(terminalWelcomeString);
	char inputMessageBuffer[inputMessageBufferLength];
	
	while(1){
		Interpreter_InString(inputMessageBuffer, inputMessageBufferLength-1);
		
		int inputLength = 0;
		for(int i = 0; i < inputMessageBufferLength && inputMessageBuffer[i] != '\0'; i++){
			inputLength++;
		}
		if(inputLength < 6){
			Interpreter_OutString(invalidCommandResult);
			continue;
		}
		
		char commandBuffer[commandLength];
		for(int i = 0; i < commandLength; i++){
			commandBuffer[i] = inputMessageBuffer[i];
		}
		commandBuffer[commandLength-1] = '\0';
		
		if(strcmp(commandBuffer, "hlp_cmd") == 0){
			Interpreter_OutString(helpStringResult);
		}else if(strcmp(commandBuffer, "lcd_top") == 0){
			// next three characters should be space num space
			// min arguments is i.e lcd_top 1 ' ' 
			if(inputMessageBufferLength < 11){
				Interpreter_OutString(invalidCommandResult);
			}else{
				if((inputMessageBuffer[7] != ' ') || 
					 (inputMessageBuffer[9] != ' ') || 
					 (inputMessageBuffer[8] - '0' > 6)){
					Interpreter_OutString(invalidNumArgsResult);
				}else{
					int lcdLineNum = inputMessageBuffer[8] - '0';
					//maximum length of string that LCD can take is 17 characters
					//use 18 to add the null terminator
					char lcdStringBuffer[23];
					for(int i = 0; i < 23; i++){
						lcdStringBuffer[i] = ' ';
					}
					
					int lcdStringBufferIdx = 0;
					for(; lcdStringBufferIdx < 23 && ((lcdStringBufferIdx+10) < inputLength) && (inputMessageBuffer[10+lcdStringBufferIdx] != '|'); lcdStringBufferIdx++){
						lcdStringBuffer[lcdStringBufferIdx] = inputMessageBuffer[10 + lcdStringBufferIdx];
					}
					if(lcdStringBufferIdx > 21){
						lcdStringBuffer[21] = '\0';
					}else{
						lcdStringBuffer[lcdStringBufferIdx] = '\0';
					}
					
					int inputMessageBufferIdx = lcdStringBufferIdx + 10 + 1;
					//inputMessageBufferIdx should be pointing to one after '|' or should be > input length if read the whole string
					//if one after '|', and '-' is the last character, then print out on the message
					if(inputMessageBufferIdx >= inputLength || ((inputMessageBufferIdx == inputLength-1) && inputMessageBuffer[inputMessageBufferIdx] == '-')){
						ST7735_Message(0, lcdLineNum, lcdStringBuffer, -10000);
					}else{
						if(inputMessageBuffer[inputMessageBufferIdx] == '-' ||
						 (inputMessageBuffer[inputMessageBufferIdx] > 47 && inputMessageBuffer[inputMessageBufferIdx] < 58)){
						
							int negativity = 1;
							if(inputMessageBuffer[inputMessageBufferIdx] == '-'){
								inputMessageBufferIdx++;
								negativity = -1;
							}
							
							int invalidNum = 0;
							char numberInputBuffer[5] = {'0', '0', '0', '0'};
							int j = 0;
							for(; (j < 4) && (inputMessageBufferIdx < inputLength); j++, inputMessageBufferIdx++){
								if(inputMessageBuffer[inputMessageBufferIdx] < 48 || inputMessageBuffer[inputMessageBufferIdx] > 57){
									invalidNum = 1;
									break;
								}
								numberInputBuffer[j] = inputMessageBuffer[inputMessageBufferIdx];
							}
							
							if(invalidNum && j == 0){
								Interpreter_OutString("\ninvalid numToDisplay\n");
							}else{
								j--;
								
								int numToDislay = 0;
								int multiplier = 1;
								while(j >= 0){
									int addend = numberInputBuffer[j] - '0';
									addend *= multiplier;
									numToDislay += addend;
									multiplier *= 10;
									j--;
								}
								
								numToDislay *= negativity;
								ST7735_Message(0, lcdLineNum, lcdStringBuffer, numToDislay);
							}
						}else{
							Interpreter_OutString("\nformat is lcd_xyz n s|n\n");
						}
					}
					Interpreter_OutString("\n");
				}
			}
		}else if(strcmp(commandBuffer, "lcd_bot") == 0){
			// next three characters should be space num space
			// min arguments is i.e lcd_top 1 ' ' 
			if(inputMessageBufferLength < 11){
				Interpreter_OutString(invalidCommandResult);
			}else{
				if((inputMessageBuffer[7] != ' ') || 
					 (inputMessageBuffer[9] != ' ') || 
					 (inputMessageBuffer[8] - '0' > 6)){
					Interpreter_OutString(invalidNumArgsResult);
				}else{
					int lcdLineNum = inputMessageBuffer[8] - '0';
					//maximum length of string that LCD can take is 17 characters
					//use 18 to add the null terminator
					char lcdStringBuffer[23];
					for(int i = 0; i < 23; i++){
						lcdStringBuffer[i] = ' ';
					}
					
					int lcdStringBufferIdx = 0;
					for(; lcdStringBufferIdx < 23 && ((lcdStringBufferIdx+10) < inputLength) && (inputMessageBuffer[10+lcdStringBufferIdx] != '|'); lcdStringBufferIdx++){
						lcdStringBuffer[lcdStringBufferIdx] = inputMessageBuffer[10 + lcdStringBufferIdx];
					}
					if(lcdStringBufferIdx > 21){
						lcdStringBuffer[21] = '\0';
					}else{
						lcdStringBuffer[lcdStringBufferIdx] = '\0';
					}
					
					int inputMessageBufferIdx = lcdStringBufferIdx + 10 + 1;
					//inputMessageBufferIdx should be pointing to one after '|' or should be > input length if read the whole string
					//if one after '|', and '-' is the last character, then print out on the message
					if(inputMessageBufferIdx >= inputLength || ((inputMessageBufferIdx == inputLength-1) && inputMessageBuffer[inputMessageBufferIdx] == '-')){
						ST7735_Message(1, lcdLineNum, lcdStringBuffer, -10000);
					}else{
						if(inputMessageBuffer[inputMessageBufferIdx] == '-' ||
						 (inputMessageBuffer[inputMessageBufferIdx] > 47 && inputMessageBuffer[inputMessageBufferIdx] < 58)){
						
							int negativity = 1;
							if(inputMessageBuffer[inputMessageBufferIdx] == '-'){
								inputMessageBufferIdx++;
								negativity = -1;
							}
							
							int invalidNum = 0;
							char numberInputBuffer[5] = {'0', '0', '0', '0'};
							int j = 0;
							for(; (j < 4) && (inputMessageBufferIdx < inputLength); j++, inputMessageBufferIdx++){
								if(inputMessageBuffer[inputMessageBufferIdx] < 48 || inputMessageBuffer[inputMessageBufferIdx] > 57){
									invalidNum = 1;
									break;
								}
								numberInputBuffer[j] = inputMessageBuffer[inputMessageBufferIdx];
							}
							
							if(invalidNum && j == 0){
								Interpreter_OutString("\ninvalid numToDisplay\n");
							}else{
								j--;
								
								int numToDislay = 0;
								int multiplier = 1;
								while(j >= 0){
									int addend = numberInputBuffer[j] - '0';
									addend *= multiplier;
									numToDislay += addend;
									multiplier *= 10;
									j--;
								}
								
								numToDislay *= negativity;
								ST7735_Message(1, lcdLineNum, lcdStringBuffer, numToDislay);
							}
						}else{
							Interpreter_OutString("\nformat is lcd_xyz n s|n\n");
						}
					}
					Interpreter_OutString("\n");
				}
			}
		}else if(strcmp(commandBuffer, "adc_inp") == 0){
			uint32_t adcValue = ADC_In();
			//maximum of 4 digits + decimal point + 'v' + null char = 7 total chars
			char adcValueString[7];
			adcValueString[6] = '\0';
			adcValueString[5] = 'v';
			adcValueString[1] = '.';
			
			char oneth = '0';
			char tenth = '0';
			char hundredth = '0';
			char thousandth = '0';
			
			thousandth += adcValue % 10;
			adcValue /= 10;
			hundredth += adcValue % 10;
			adcValue /= 10;
			tenth += adcValue % 10;
			adcValue /= 10;
			oneth += adcValue % 10;
			
			adcValueString[0] = oneth;
			adcValueString[2] = tenth;
			adcValueString[3] = hundredth;
			adcValueString[4] = thousandth;
			
			Interpreter_OutString("\n");
			Interpreter_OutString(adcValueString);
			Interpreter_OutString("\n");
			
		}else if(strcmp(commandBuffer, "get_tme") == 0){
			uint32_t time = OS_MsTime();
			//7 digits can support > 1 hour + 'm' + 's' + '\0' = 10 chars needed
			char timeString[10];
			for(int i = 0; i < 10; i++){
				timeString[i] = '0';
			}
			
			timeString[9] = '\0';
			timeString[8] = 's';
			timeString[7] = 'm';
			
			for(int i = 6; i > 0 && time > 0; i--, time /=10){
				timeString[i] = '0' +(time % 10);
			}
			Interpreter_OutString("\n");
			Interpreter_OutString(timeString);
			Interpreter_OutString("\n");
			
		}else if(strcmp(commandBuffer, "clr_tme") == 0){
			OS_ClearMsTime();
			Interpreter_OutString("\n");
		}else if(strcmp(commandBuffer, "jit_his") == 0){
			Jitter(MaxJitter, JitterSize, JitterHistogram);
		}else if(strcmp(commandBuffer, "prt_dir") == 0){
			PrintDirectory();
		}else if(strcmp(commandBuffer, "prt_fil") == 0){
			Interpreter_OutString("\n\r");
			Interpreter_OutString("type in file name, 0-7 characters : ");
			UART_InString(eFileNameBuffer, eFileNameLength-1);
			PrintFile(eFileNameBuffer);
		}else if(strcmp(commandBuffer, "del_fil") == 0){
			Interpreter_OutString("\n\r");
			Interpreter_OutString("type in name of file to delete, 0-7 characters : ");
			UART_InString(eFileNameBuffer, eFileNameLength-1);
			DeleteFile(eFileNameBuffer);
		}else if(strcmp(commandBuffer, "fmt_dsk") == 0){
			FormatDisk();
		}else if(strcmp(commandBuffer, "crt_fil") == 0){
			Interpreter_OutString("\n\r");
			Interpreter_OutString("type in name of file to create, 0-7 characters : ");
			UART_InString(eFileNameBuffer, eFileNameLength-1);
			CreateFile(eFileNameBuffer);
		}else if(strcmp(commandBuffer, "wrt_fil") == 0){
			Interpreter_OutString("\n\r");
			Interpreter_OutString("type in name of file to write to, 0-7 characters : ");
			UART_InString(eFileNameBuffer, eFileNameLength-1);
			Interpreter_OutString("\n\r");
			Interpreter_OutString("enter string to write: 0-100 characters : ");
			UART_InString(eFileWriteBuffer, eFileWriteLength-1);
			WriteToFile(eFileNameBuffer, eFileWriteBuffer);
		}else if(strcmp(commandBuffer, "umt_fil") == 0){
			UnmountFS();
		}else if(strcmp(commandBuffer, "lod_prg") == 0){
			LoadProg(userProgNameBuffer, userProgNameLength);
		}else if(strcmp(commandBuffer, "led_001") == 0){
			interpreterLED(1);
		}else if(strcmp(commandBuffer, "led_010") == 0){
			interpreterLED(2);
		}else if(strcmp(commandBuffer, "led_011") == 0){
			interpreterLED(3);
		}else if(strcmp(commandBuffer, "ext_tnt") == 0){
			if(exitTelnetSession()){
				return;
			}
		}else if(strcmp(commandBuffer, "sel_scl") == 0){
			selectServerClient();
			if(serverClientStatus == 0 || serverClientStatus == 1){
				// finish arbitration
				return;
			}
		}else{
			Interpreter_OutString(invalidCommandResult);
		}
		clearInputMessageBuffer();
	}
}


