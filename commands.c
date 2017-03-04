//*****************************************************************************
//
// commands.c - Implementa el interprete de comandos. Para ello utiliza las bibliotecas
// cmdline.c y uartsdio.c
//
//
//*****************************************************************************


#include <stdbool.h>
#include <stdint.h>

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Standard TIVA includes */
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"

#include "driverlib/debug.h"
#include "driverlib/gpio.h"
#include "driverlib/uart.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "rom_patch.h"
#include "pin.h"
/* Other TIVA includes */
#include "utils/cpu_usage.h"
#include "utils/cmdline.h"


#include "utils/uartstdio.h"

#include "simplelink.h"
// common interface includes
#include "common.h"
#include "uart_if.h"
#include "network_if.h"



// ==============================================================================
// The CPU usage in percent, in 16.16 fixed point format.
// ==============================================================================
extern uint32_t g_ui32CPUUsage;

// ==============================================================================
// Implementa el comando cpu que muestra el uso de CPU
// ==============================================================================
int  Cmd_cpu(int argc, char *argv[])
{
    //
    // Print some header text.
    //
    UARTprintf("ARM Cortex-M4F %u MHz - ",configCPU_CLOCK_HZ / 1000000);
    UARTprintf("%2u%% de uso\n", ((g_ui32CPUUsage+32768) >> 16));

    // Return success.
    return(0);
}

// ==============================================================================
// Implementa el comando free, que muestra cuanta memoria "heap" le queda al FreeRTOS
// ==============================================================================
int Cmd_free(int argc, char *argv[])
{
    //
    // Print some header text.
    //
#ifdef HEAP_3
	UARTprintf("Estas usando HEAP3, no es posible saberlo\n");
#else
    UARTprintf("%d bytes libres\n", xPortGetFreeHeapSize());
#endif

    // Return success.
    return(0);
}

// ==============================================================================
// Implementa el comando task. Sï¿½lo es posible si la opciï¿½n configUSE_TRACE_FACILITY de FreeRTOS estï¿½ habilitada
// ==============================================================================
#if ( configUSE_TRACE_FACILITY == 1 )

extern char *__stack;
int Cmd_tasks(int argc, char *argv[])
{
	char*	pcBuffer;
	uint8_t*	pi8Stack;
	portBASE_TYPE	x;
	
	pcBuffer = pvPortMalloc(1024);
	vTaskList(pcBuffer);
	UARTprintf("\t\t\t\tUnused\nTaskName\tStatus\tPri\tStack\tTask ID\n");
	UARTprintf("=======================================================\n");
	UARTprintf("%s", pcBuffer);
	
	// Calculate kernel stack usage
	x = 0;
	pi8Stack = (uint8_t *) &__stack;
	while (*pi8Stack++ == 0xA5)
	{
		x++;	//Esto sólo funciona si hemos rellenado la pila del sistema con 0xA5 en el arranque
	}
	sprintf((char *) pcBuffer, "%%%us", configMAX_TASK_NAME_LEN);
	sprintf((char *) &pcBuffer[10], (const char *) pcBuffer, "kernel");
	UARTprintf("%s\t-\t*%u\t%u\t-\n", &pcBuffer[10], configKERNEL_INTERRUPT_PRIORITY, x/sizeof(portBASE_TYPE));
	vPortFree(pcBuffer);
	return 0;
}

#endif /* configUSE_TRACE_FACILITY */

#if configGENERATE_RUN_TIME_STATS
// ==============================================================================
// Implementa el comando "Stats"
// ==============================================================================
Cmd_stats(int argc, char *argv[])
{
	char*	pBuffer;

	pBuffer = pvPortMalloc(1024);
	if (pBuffer)
	{
		vTaskGetRunTimeStats(pBuffer); //Es un poco inseguro, pero por ahora nos vale...
		UARTprintf("TaskName\tCycles\t\tPercent\r\n");
		UARTprintf("===============================================\r\n");
		UARTprintf("%s", pBuffer);
		vPortFree(pBuffer);
	}
	return 0;
}
#endif

// ==============================================================================
// Implementa el comando help
// ==============================================================================
int Cmd_help(int argc, char *argv[])
{
    tCmdLineEntry *pEntry;

    //
    // Print some header text.
    //
    UARTprintf("Comandos disponibles\n");
    UARTprintf("------------------\n");

    //
    // Point at the beginning of the command table.
    //
    pEntry = &g_psCmdTable[0];

    //
    // Enter a loop to read each entry from the command table.  The end of the
    // table has been reached when the command name is NULL.
    //
    while(pEntry->pcCmd)
    {
        //
        // Print the command name and the brief description.
        //
        UARTprintf("%s%s\n", pEntry->pcCmd, pEntry->pcHelp);

        //
        // Advance to the next entry in the table.
        //
        pEntry++;
    }

    //
    // Return success.
    //
    return(0);
}


int Cmd_station(int argc, char *argv[])
{
	int lRetVal;

	if (argc != 4)
	{
		//Si los parametros no son suficientes o son demasiados, muestro la ayuda
		UARTprintf(" station [ssid] [sec] [password]\n");
	}
	else
	{
	    lRetVal = Network_IF_InitDriver(ROLE_STA);
	    if (lRetVal<0) ERR_PRINT( lRetVal);


	    SlSecParams_t SecurityParams = {0};

	    // Initialize AP security params
	    SecurityParams.Key = argv[3];
	    SecurityParams.KeyLen = strlen(argv[3]);

	    if ((0==strncmp( argv[2], "WEP",3))||(0==strncmp( argv[2], "wep",3)))
	    {
	    	SecurityParams.Type = SL_SEC_TYPE_WEP;
	    }
	    else if ((0==strncmp( argv[2], "WPA",3))||(0==strncmp( argv[2], "wpa",3)))
	    {
	    	SecurityParams.Type = SL_SEC_TYPE_WPA_WPA2;
	    }
	    else
	    {
	    	SecurityParams.Type = SL_SEC_TYPE_OPEN;
	    }

	    //
	    // Connect to the Access Point
	    //
	    lRetVal = Network_IF_ConnectAP(argv[1], SecurityParams);
	    if (lRetVal<0) ERR_PRINT( lRetVal);

	}
    return 0;
}

int Cmd_mode(int argc, char *argv[])
{
	if (argc != 2)
	{
		//Si los parametros no son suficientes o son demasiados, muestro la ayuda
		UARTprintf(" mode [sensor|led]\n");
	}
	else
	{
		//seconds = ustrtoul(argv[1], NULL, 10);

		if (0==strncmp( argv[1], "sensor",5))
		{
			UARTprintf("Modo sensores\n");
		    //This disconfigures PIN1 and 2 for LEDs...
		    // Configure PIN_01 for I2C0 I2C_SCL
		    //
		    MAP_PinTypeI2C(PIN_01, PIN_MODE_1);

		    //
		    // Configure PIN_02 for I2C0 I2C_SDA
		    //
		    MAP_PinTypeI2C(PIN_02, PIN_MODE_1);
		}
		else if (0==strncmp( argv[1], "led",3))
		{
			UARTprintf("Modo LED\n");

		    //
		    // Configure PIN_01 for GPIOOutput
		    //
		    MAP_PinTypeGPIO(PIN_01, PIN_MODE_0, false);
		    MAP_GPIODirModeSet(GPIOA1_BASE, 0x4, GPIO_DIR_MODE_OUT);

		    //
		    // Configure PIN_02 for GPIOOutput
		    //
		    MAP_PinTypeGPIO(PIN_02, PIN_MODE_0, false);
		    MAP_GPIODirModeSet(GPIOA1_BASE, 0x8, GPIO_DIR_MODE_OUT);
			//No deberia hacerlo mientras haya una transferencia I2C activa.

		}
		else
		{
			//Si el parametro no es correcto, muestro la ayuda
			UARTprintf(" led [sensor|led]\n");
		}

	}
    return 0;
}
#include"bma222drv.h"
#include"tmp006drv.h"

// ==============================================================================
// Implementa el comando "LED"
// ==============================================================================
int Cmd_read(int argc, char *argv[])
{
	signed char cAccXT2,cAccYT2,cAccZT2;
	if (argc != 2)
	{
		//Si los parametros no son suficientes o son demasiados, muestro la ayuda
		UARTprintf(" read [acc|mp]\n");
	}
	else
	{
		//seconds = ustrtoul(argv[1], NULL, 10);

		if (0==strncmp( argv[1], "acc",3))
		{
			UARTprintf("Lee aceleracion\n");
			BMA222ReadNew(&cAccXT2, &cAccYT2, &cAccZT2);
			UARTprintf("%i %i %i\n",(int)cAccXT2,(int)cAccYT2,(int)cAccZT2);
		}
		else if (0==strncmp( argv[1], "tmp",3))
		{
			float temp;
			char cadena[20];
	        int longitud;
			UARTprintf("Lee temperatura\n");
			TMP006DrvGetTemp(&temp);
			longitud=sprintf(cadena,"%f\n",temp);
			if (longitud>0) UARTwrite(cadena,longitud);
		}
		else
		{
			//Si el parametro no es correcto, muestro la ayuda
			UARTprintf(" read [acc|tmp]\n");
		}

	}

    return 0;
}

#ifndef STATION_MODE
//MQTT Task (Para el caso en el que no estoy en modo "STATION"
extern void MqttClientTask(void *pvParameters);

int Cmd_mqtt(int argc, char *argv[])
{
	if (argc != 2)
	{
		//Si los parametros no son suficientes o son demasiados, muestro la ayuda
		UARTprintf(" mqtt [on|off]\n");
	}
	else
	{
		//seconds = ustrtoul(argv[1], NULL, 10);

		if (0==strncmp( argv[1], "on",4))
		{
			UARTprintf("Inicio la tarea MQTT \n");
			//DEBERIA COMPROBAS QUE NO ESTA INICIADA!!!
		    if (osi_TaskCreate(MqttClientTask, (const signed char *)"MQTT Task", 2048, NULL, 2, NULL )<0)
		    {
		        LOOP_FOREVER();
		    }
		}
		else if (0==strncmp( argv[1], "off",3))
		{
			UARTprintf("Finalizo la tarea MQTT \n");
			//NO IMPLEMENTADA!!!
			//
		}
		else
		{
			//Si el parametro no es correcto, muestro la ayuda
			UARTprintf(" mqtt [on|off]\n");
		}
	}


    return 0;
}

#endif


// ==============================================================================
// Tabla con los comandos y su descripcion. Si quiero anadir alguno, debo hacerlo aqui
// ==============================================================================
tCmdLineEntry g_psCmdTable[] =
{
    { "help",     Cmd_help,      "     : Lista de comandos" },
    { "?",        Cmd_help,      "        : lo mismo que help" },
    { "cpu",      Cmd_cpu,       "      : Muestra el uso de  CPU " },
	{ "read",  	  Cmd_read,       "      : Lee de los sensores I2C" },
	{ "mode",  	  Cmd_mode,       "      : Cambia los pines de modo" },
    { "free",     Cmd_free,      "     : Muestra la memoria libre" },
	{ "station",     Cmd_station,      "     : cambia al modo station" },
#ifndef STATION_MODE
	{ "mqtt",     Cmd_mqtt,      "     : arranca el servidor MQTT" },
#endif
#if ( configUSE_TRACE_FACILITY == 1 )
	{ "tasks",    Cmd_tasks,     "    : Muestra informacion de las tareas" },
#endif
#if (configGENERATE_RUN_TIME_STATS)
	{ "stats",    Cmd_stats,      "     : Muestra estadisticas de las tareas" },
#endif
    { 0, 0, 0 }
};

// ==============================================================================
// Tarea UARTTask.  Espera la llegada de comandos por el puerto serie y los ejecuta al recibirlos...
//					Por tanto, es la que implementa el interprete de comandos.
// ==============================================================================

void vUARTTask( void *pvParameters )
{
    char    pcCmdBuf[64];
    int32_t i32Status;

	UARTprintf("\n\n FreeRTOS %s \n",
		tskKERNEL_VERSION_NUMBER);
	UARTprintf("\n Teclee ? para ver la ayuda \n");
	UARTprintf("> ");

	/* Loop forever */
    while (1)
    {
	    	/* Read data from the UART and process the command line */
	        UARTgets(pcCmdBuf, sizeof(pcCmdBuf));	//UARTgets es bloqueante porque usa internamente una cola de mensaje
	        if (strlen(pcCmdBuf) == 0)
	        {
	            UARTprintf("> ");
	            continue;
	        }
	
	        //
	        // Pass the line from the user to the command processor.  It will be
	        // parsed and valid commands executed.
	        //
	        i32Status = CmdLineProcess(pcCmdBuf);
	
	        //
	        // Handle the case of bad command.
	        //
	        if(i32Status == CMDLINE_BAD_CMD)
	        {
	            UARTprintf("Comando erroneo!\n");	//No pongo acentos adrede
	        }
	
	        //
	        // Handle the case of too many arguments.
	        //
	        else if(i32Status == CMDLINE_TOO_MANY_ARGS)
	        {
	            UARTprintf("El interprete de comandos no admite tantos parametros\n");	//El maximo, CMDLINE_MAX_ARGS, esta definido en cmdline.c
	        }
	
	        //
	        // Otherwise the command was executed.  Print the error code if one was
	        // returned.
	        //
	        else if(i32Status != 0)
	        {
	            UARTprintf("El comando devolvio el error %d\n",i32Status);
	        }
	
	        UARTprintf("> ");
    }
}
