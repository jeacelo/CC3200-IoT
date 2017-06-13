//*****************************************************************************
// Copyright (C) 2016 Texas Instruments Incorporated
//
// All rights reserved. Property of Texas Instruments Incorporated.
// Restricted rights to use, duplicate or disclose this code are
// granted through contract.
// The program may not be used without the written permission of
// Texas Instruments Incorporated or against the terms and conditions
// stipulated in the agreement under which this program has been supplied,
// and under no circumstances can it be used with non-TI connectivity device.
//
//*****************************************************************************
//*****************************************************************************
// Modificado, adaptado y ampliado por Jose Manuel Cano, Eva Gonzalez, Ignacio Herrero
// Departamento de Tecnologia Electronica
// Universidad de Malaga
//*****************************************************************************
//
//*****************************************************************************
//
// Application Name     -   MQTT Client
// Application Overview -   This application acts as a MQTT client and connects
//                          to the IBM MQTT broker, simultaneously we can
//                          connect a web client from a web browser. Both
//                          clients can inter-communicate using appropriate
//                          topic names.
//
// Application Details  -
// http://processors.wiki.ti.com/index.php/CC32xx_MQTT_Client
// or
// docs\examples\CC32xx_MQTT_Client.pdf
//
//*****************************************************************************

//*****************************************************************************
//
//! \addtogroup mqtt_client
//! @{
//
//*****************************************************************************

#include<stdint.h>
#include<stdbool.h>
#include<math.h>
#include<string.h>

// Standard includes
#include <stdlib.h>

// simplelink includes
#include "simplelink.h"

// driverlib includes
#include "hw_types.h"
#include "hw_ints.h"
#include "hw_memmap.h"
#include "interrupt.h"
#include "rom_map.h"
#include "prcm.h"
#include "uart.h"
#include "gpio.h"
#include "pin.h"
#include "timer.h"

// common interface includes
#include "network_if.h"
#ifndef NOTERM
#include "uart_if.h"
#endif

#include "button_if.h"
#include "gpio_if.h"
#include "timer_if.h"
#include "common.h"
#include "utils.h"

#include "utils/uartstdio.h"
#include "i2c_if.h"
#include "pwm_if.h"

//Sensores
#include "tmp006drv.h"
#include "bma222drv.h"


#include "sl_mqtt_client.h"

// application specific includes
#include "pinmux.h"

#include "frozen.h" //Cabecera JSON

//Librerias para control de iluminacion WS2812B
#include <WS2812/example/samplePatterns.h>
#include <WS2812/SPI_uDMA_drv.h>
#include <WS2812/WS2812_drv.h>

#define APPLICATION_VERSION 	"1.1.1"

/*Operate Lib in MQTT 3.1 mode.*/
#define MQTT_3_1_1              false /*MQTT 3.1.1 */
#define MQTT_3_1                true /*MQTT 3.1*/

#define WILL_TOPIC              "Client"
#define WILL_MSG                "Client Stopped"
#define WILL_QOS                QOS2
#define WILL_RETAIN             false

/*Defining Broker IP address and port Number*/
#define SERVER_ADDRESS          "192.168.1.13"
//#define SERVER_ADDRESS          "192.168.1.2"
#define PORT_NUMBER             1883

#define MAX_BROKER_CONN         1

#define SERVER_MODE             MQTT_3_1
/*Specifying Receive time out for the Receive task*/
#define RCV_TIMEOUT             30

/*Background receive task priority*/
#define TASK_PRIORITY           3

/* Keep Alive Timer value*/
#define KEEP_ALIVE_TIMER        25

/*Clean session flag*/
#define CLEAN_SESSION           true

/*Retain Flag. Used in publish message. */
#define RETAIN                  1

/*Defining Number of topics*/
#define TOPIC_COUNT             1

/*Defining Subscription Topic Values*/
#define TOPIC_CONFIG                "/cc3200/Config"

/*Defining QOS levels*/
#define QOS0                    0
#define QOS1                    1
#define QOS2                    2

/*Spawn task priority and OSI Stack Size*/
#define OSI_STACK_SIZE          2048
#define UART_PRINT              Report

#define NUM_LEDS				22

typedef struct connection_config{
    SlMqttClientCtxCfg_t broker_config;
    void *clt_ctx;
    unsigned char *client_id;
    unsigned char *usr_name;
    unsigned char *usr_pwd;
    bool is_clean;
    unsigned int keep_alive_time;
    SlMqttClientCbs_t CallBAcks;
    int num_topics;
    char *topic[TOPIC_COUNT];
    unsigned char qos[TOPIC_COUNT];
    SlMqttWill_t will_params;
    bool is_connected;
}connect_config;

typedef enum events
{
	READ_TEMP,
	READ_ACC,
    BROKER_DISCONNECTION
}osi_messages;


#ifdef USE_FREERTOS
void vUARTTask( void *pvParameters );
#endif


//*****************************************************************************
//                      LOCAL FUNCTION PROTOTYPES
//*****************************************************************************
static void
Mqtt_Recv(void *app_hndl, const char  *topstr, long top_len, const void *payload,
          long pay_len, bool dup,unsigned char qos, bool retain);
static void sl_MqttEvt(void *app_hndl,long evt, const void *buf,
                       unsigned long len);
static void sl_MqttDisconnect(void *app_hndl);
void TimerPeriodicIntHandler(void);
void LedTimerConfigNStart();
void LedTimerDeinitStop();
void BoardInit(void);
static void DisplayBanner(char * AppName);
void ConnectWiFI(void *pvParameters);
void TempTask(void *pvParameters);
void AccTask(void *pvParameters);
void SubTask();
//*****************************************************************************
//                 GLOBAL VARIABLES -- Start
//*****************************************************************************
#ifdef USE_FREERTOS
#if defined(ewarm)
extern uVectorEntry __vector_table;
#endif
#if defined(ccs)
extern void (* const g_pfnVectors[])(void);
#endif
#endif

unsigned short g_usTimerInts;
/* AP Security Parameters */
SlSecParams_t SecurityParams = {0};

/*Message Queue*/
OsiMsgQ_t g_PBQueue;

/* connection configuration */
connect_config usr_connect_config[] =
{
    {
        {
            {
                SL_MQTT_NETCONN_URL,
                SERVER_ADDRESS,
                PORT_NUMBER,
                0,
                0,
                0,
                NULL
            },
            SERVER_MODE,
            true,
        },
        NULL,
        (unsigned char *)"user1",
        NULL,
        NULL,
        true,
        KEEP_ALIVE_TIMER,
        {Mqtt_Recv, sl_MqttEvt, sl_MqttDisconnect},
        TOPIC_COUNT,
        {TOPIC_CONFIG}, /* Tantos como TOPIC_COUNT */
        {QOS2}, /* Tantos como topics */
        {WILL_TOPIC,WILL_MSG,WILL_QOS,WILL_RETAIN},
        false
    }
};

/* library configuration */
SlMqttClientLibCfg_t Mqtt_Client={
    1882,
    TASK_PRIORITY,
    30,
    true,
    (long(*)(const char *, ...))UART_PRINT
};

/*Publishing topics and messages*/
const char *topic_root = "/cc3200/";
char *sub_topic_leds = "initializated";
char *sub_topic_sensors = "initializated";
char *pub_topic_temp = "initializated";
char *pub_topic_acc = "initializated";

void *app_hndl = (void*)usr_connect_config;

static uint8_t pui8Colors[NUM_LEDS][3];
static uint8_t pui8SPIOut[NUM_LEDS][WS2812_SPI_BYTE_PER_CLR *
	                                  WS2812_SPI_BIT_WIDTH];

float temp = 0;
signed char accX = 0;
signed char accY = 0;
signed char accZ = 0;

int ref_temp, ref_acc;

OsiTaskHandle TemppTaskHandle = NULL, AccpTaskHandle = NULL, SubTaskHandle = NULL;

bool topic_leds = false, topic_sensors = false, topic_temp = false, topic_acc = false;

//GLOBAL PARA DE MOMENTO NO GASTAR PILA (CUIDADO!!!)
char json_buffer[100];
//struct json_out out1 = JSON_OUT_BUF(json_buffer, sizeof(json_buffer));

//*****************************************************************************
//                 GLOBAL VARIABLES -- End
//*****************************************************************************

//****************************************************************************
//! Defines Mqtt_Pub_Message_Receive event handler.
//! Client App needs to register this event handler with sl_ExtLib_mqtt_Init 
//! API. Background receive task invokes this handler whenever MQTT Client 
//! receives a Publish Message from the broker.
//!
//!\param[out]     topstr => pointer to topic of the message
//!\param[out]     top_len => topic length
//!\param[out]     payload => pointer to payload
//!\param[out]     pay_len => payload length
//!\param[out]     retain => Tells whether its a Retained message or not
//!\param[out]     dup => Tells whether its a duplicate message or not
//!\param[out]     qos => Tells the Qos level
//!
//!\return none
//****************************************************************************
static void
Mqtt_Recv(void *app_hndl, const char  *topstr, long top_len, const void *payload,
                       long pay_len, bool dup,unsigned char qos, bool retain)
{
    int refresh;
    char *output_str=(char*)pvPortMalloc(top_len+1);
    memset(output_str,'\0',top_len+1);
    strncpy(output_str, (char*)topstr, top_len);
    output_str[top_len]='\0';
    int red, green, blue;
    int index;
    long lRetVal = -1;

    if(strncmp(output_str,sub_topic_leds, top_len) == 0)
    {
    	if (json_scanf((const char *)payload, pay_len, "{ LED: %d R: %d G: %d B: %d }", &index, &red, &green, &blue)>0)
    	{
    		waitSPITramsfer();
    		osi_Sleep(100);
    		pui8Colors[index][0]=green;
    		pui8Colors[index][1]=red;
    		pui8Colors[index][2]=blue;
    		WSGRBtoSPI(pui8SPIOut[index], pui8Colors[index][0],
    				pui8Colors[index][1], pui8Colors[index][2]);
    		InitSPITransfer((uint8_t*)pui8SPIOut, sizeof(pui8SPIOut));
    	}
    }
    else if(strncmp(output_str,sub_topic_sensors, top_len) == 0)
    {
        if (json_scanf((const char *)payload, pay_len, "{ TEMP: %d }", &refresh)>0)
        {
            if (refresh>0)
            {
                ref_temp = refresh;
                if (TemppTaskHandle == NULL)
                {
                    lRetVal = osi_TaskCreate(TempTask,
                            (const signed char *)"TempTask",
                            OSI_STACK_SIZE, &ref_temp, 2, &TemppTaskHandle );

                    if(lRetVal < 0)
                    {
                        ERR_PRINT(lRetVal);
                        LOOP_FOREVER();
                    }
                }
            }
            else if (refresh==0)
            {
                if (TemppTaskHandle != NULL)
                {
                    osi_TaskDelete(&TemppTaskHandle);
                    TemppTaskHandle = NULL;
                }
            }
        }
        if (json_scanf((const char *)payload, pay_len, "{ ACC: %d }", &refresh)>0)
        {
            if (refresh>0)
            {
                ref_acc = refresh;
                if (AccpTaskHandle == NULL)
                {
                    lRetVal = osi_TaskCreate(AccTask,
                            (const signed char *)"AccTask",
                            OSI_STACK_SIZE, &ref_acc, 2, &AccpTaskHandle);

                    if(lRetVal < 0)
                    {
                        ERR_PRINT(lRetVal);
                        LOOP_FOREVER();
                    }
                }
            }
            else if (refresh==0)
            {
                if (AccpTaskHandle != NULL)
                {
                    osi_TaskDelete(&AccpTaskHandle);
                    AccpTaskHandle = NULL;
                }
            }
        }
    }
    else if (strncmp(output_str,TOPIC_CONFIG, top_len) == 0)
    {
        if (json_scanf((const char *)payload, pay_len, "{ LEDS: %Q }", &sub_topic_leds)>0)
        {
            topic_leds = true;
        }
        if (json_scanf((const char *)payload, pay_len, "{ SENSORS: %Q }", &sub_topic_sensors)>0)
        {
            topic_sensors = true;
        }
        if (json_scanf((const char *)payload, pay_len, "{ TEMP: %Q }", &pub_topic_temp)>0)
        {
            topic_temp = true;
        }
        if (json_scanf((const char *)payload, pay_len, "{ ACC: %Q }", &pub_topic_acc)>0)
        {
            topic_acc = true;
        }
        if (topic_leds || topic_sensors || topic_temp || topic_acc)
        {
            lRetVal = osi_TaskCreate(SubTask,
                    (const signed char *)"SubTask",
                    OSI_STACK_SIZE, NULL, 2, &SubTaskHandle);

            if(lRetVal < 0)
            {
                ERR_PRINT(lRetVal);
                LOOP_FOREVER();
            }
        }
    }

    UART_PRINT("\n\rPublish Message Received");
    UART_PRINT("\n\rTopic: ");
    UART_PRINT("%s",output_str);
    vPortFree(output_str);
    UART_PRINT(" [Qos: %d] ",qos);
    if(retain)
      UART_PRINT(" [Retained]");
    if(dup)
      UART_PRINT(" [Duplicate]");
    
    output_str=(char*)pvPortMalloc(pay_len+1);
    memset(output_str,'\0',pay_len+1);
    strncpy(output_str, (char*)payload, pay_len);
    output_str[pay_len]='\0';
    UART_PRINT("\n\rData is: ");
    UART_PRINT("%s",(char*)output_str);
    UART_PRINT("\n\r");
    vPortFree(output_str);
    
    return;
}

//****************************************************************************
//! Defines sl_MqttEvt event handler.
//! Client App needs to register this event handler with sl_ExtLib_mqtt_Init 
//! API. Background receive task invokes this handler whenever MQTT Client 
//! receives an ack(whenever user is in non-blocking mode) or encounters an error.
//!
//! param[out]      evt => Event that invokes the handler. Event can be of the
//!                        following types:
//!                        MQTT_ACK - Ack Received 
//!                        MQTT_ERROR - unknown error
//!                        
//!  
//! \param[out]     buf => points to buffer
//! \param[out]     len => buffer length
//!       
//! \return none
//****************************************************************************
static void
sl_MqttEvt(void *app_hndl, long evt, const void *buf,unsigned long len)
{
    int i;
    switch(evt)
    {
      case SL_MQTT_CL_EVT_PUBACK:
        UART_PRINT("PubAck:\n\r");
        UART_PRINT("%s\n\r",buf);
        break;
    
      case SL_MQTT_CL_EVT_SUBACK:
        UART_PRINT("\n\rGranted QoS Levels are:\n\r");
        
        for(i=0;i<len;i++)
        {
          UART_PRINT("QoS %d\n\r",((unsigned char*)buf)[i]);
        }
        break;
        
      case SL_MQTT_CL_EVT_UNSUBACK:
        UART_PRINT("UnSub Ack \n\r");
        UART_PRINT("%s\n\r",buf);
        break;
    
      default:
        break;
  
    }
}

//****************************************************************************
//
//! callback event in case of MQTT disconnection
//!
//! \param app_hndl is the handle for the disconnected connection
//!
//! return none
//
//****************************************************************************
static void
sl_MqttDisconnect(void *app_hndl)
{
    connect_config *local_con_conf;
    osi_messages var = BROKER_DISCONNECTION;
    local_con_conf = (connect_config *)app_hndl;
    sl_ExtLib_MqttClientUnsub(local_con_conf->clt_ctx, local_con_conf->topic,
                              TOPIC_COUNT);
    UART_PRINT("disconnect from broker %s\r\n",
           (local_con_conf->broker_config).server_info.server_addr);
    local_con_conf->is_connected = false;
    sl_ExtLib_MqttClientCtxDelete(local_con_conf->clt_ctx);

    //
    // write message indicating publish message
    //
    osi_MsgQWrite(&g_PBQueue,&var,OSI_NO_WAIT);

}

//*****************************************************************************
//
//! Periodic Timer Interrupt Handler
//!
//! \param None
//!
//! \return None
//
//*****************************************************************************
void
TimerPeriodicIntHandler(void)
{
    unsigned long ulInts;

    //
    // Clear all pending interrupts from the timer we are
    // currently using.
    //
    ulInts = MAP_TimerIntStatus(TIMERA0_BASE, true);
    MAP_TimerIntClear(TIMERA0_BASE, ulInts);

    //
    // Increment our interrupt counter.
    //
    g_usTimerInts++;
    if(!(g_usTimerInts & 0x1))
    {
        //
        // Off Led
        //
        GPIO_IF_LedOff(MCU_RED_LED_GPIO);
    }
    else
    {
        //
        // On Led
        //
        GPIO_IF_LedOn(MCU_RED_LED_GPIO);
    }
}

//****************************************************************************
//
//! Function to configure and start timer to blink the LED while device is
//! trying to connect to an AP
//!
//! \param none
//!
//! return none
//
//****************************************************************************
void LedTimerConfigNStart()
{
    //
    // Configure Timer for blinking the LED for IP acquisition
    //
    Timer_IF_Init(PRCM_TIMERA0,TIMERA0_BASE,TIMER_CFG_PERIODIC,TIMER_A,0);
    Timer_IF_IntSetup(TIMERA0_BASE,TIMER_A,TimerPeriodicIntHandler);
    Timer_IF_Start(TIMERA0_BASE,TIMER_A,100);
}

//****************************************************************************
//
//! Disable the LED blinking Timer as Device is connected to AP
//!
//! \param none
//!
//! return none
//
//****************************************************************************
void LedTimerDeinitStop()
{
    //
    // Disable the LED blinking Timer as Device is connected to AP
    //
    Timer_IF_Stop(TIMERA0_BASE,TIMER_A);
    Timer_IF_DeInit(TIMERA0_BASE,TIMER_A);

}

//*****************************************************************************
//
//! Board Initialization & Configuration
//!
//! \param  None
//!
//! \return None
//
//*****************************************************************************
void BoardInit(void)
{
    /* In case of TI-RTOS vector table is initialize by OS itself */
    #ifndef USE_TIRTOS
    //
    // Set vector table base
    //
    #if defined(ccs)
        IntVTableBaseSet((unsigned long)&g_pfnVectors[0]);
    #endif
    #if defined(ewarm)
        IntVTableBaseSet((unsigned long)&__vector_table);
    #endif
    #endif
    //
    // Enable Processor
    //
    MAP_IntMasterEnable();
    MAP_IntEnable(FAULT_SYSTICK);

    PRCMCC3200MCUInit();
}

//*****************************************************************************
//
//! Application startup display on UART
//!
//! \param  none
//!
//! \return none
//!
//*****************************************************************************
static void
DisplayBanner(char * AppName)
{

    UART_PRINT("\n\n\n\r");
    UART_PRINT("\t\t *************************************************\n\r");
    UART_PRINT("\t\t    CC3200 %s Application       \n\r", AppName);
    UART_PRINT("\t\t *************************************************\n\r");
    UART_PRINT("\n\n\n\r");
}
  
#ifndef STATION_MODE
//MQTT Task (Para el caso en el que no estoy en modo "STATION"
#ifdef __cplusplus
extern "C" {
void MqttClientTask(void *pvParameters);
}
#endif /* __cplusplus */

void MqttClientTask(void *pvParameters)
{
	long lRetVal = -1;
	int iCount = 0;
	    int iNumBroker = 0;
	    int iConnBroker = 0;
	    osi_messages RecvQue;

	    connect_config *local_con_conf = (connect_config *)app_hndl;

	//
	    // Initialze MQTT client lib
	    //
	    lRetVal = sl_ExtLib_MqttClientInit(&Mqtt_Client);
	    if(lRetVal != 0)
	    {
	        // lib initialization failed
	        UART_PRINT("MQTT Client lib initialization failed\n\r");
	        LOOP_FOREVER();
	    }

	    /******************* connection to the broker ***************************/
	    iNumBroker = sizeof(usr_connect_config)/sizeof(connect_config);
	    if(iNumBroker > MAX_BROKER_CONN)
	    {
	        UART_PRINT("Num of brokers are more then max num of brokers\n\r");
	        LOOP_FOREVER();
	    }

	    while(iCount < iNumBroker)
	    {
	        //create client context
	        local_con_conf[iCount].clt_ctx =
	        sl_ExtLib_MqttClientCtxCreate(&local_con_conf[iCount].broker_config,
	                                      &local_con_conf[iCount].CallBAcks,
	                                      &(local_con_conf[iCount]));

	        //
	        // Set Client ID
	        //
	        sl_ExtLib_MqttClientSet((void*)local_con_conf[iCount].clt_ctx,
	                            SL_MQTT_PARAM_CLIENT_ID,
	                            local_con_conf[iCount].client_id,
	                            strlen((char*)(local_con_conf[iCount].client_id)));

	        //
	        // Set will Params
	        //
	        if(local_con_conf[iCount].will_params.will_topic != NULL)
	        {
	            sl_ExtLib_MqttClientSet((void*)local_con_conf[iCount].clt_ctx,
	                                    SL_MQTT_PARAM_WILL_PARAM,
	                                    &(local_con_conf[iCount].will_params),
	                                    sizeof(SlMqttWill_t));
	        }

	        //
	        // setting username and password
	        //
	        if(local_con_conf[iCount].usr_name != NULL)
	        {
	            sl_ExtLib_MqttClientSet((void*)local_con_conf[iCount].clt_ctx,
	                                SL_MQTT_PARAM_USER_NAME,
	                                local_con_conf[iCount].usr_name,
	                                strlen((char*)local_con_conf[iCount].usr_name));

	            if(local_con_conf[iCount].usr_pwd != NULL)
	            {
	                sl_ExtLib_MqttClientSet((void*)local_con_conf[iCount].clt_ctx,
	                                SL_MQTT_PARAM_PASS_WORD,
	                                local_con_conf[iCount].usr_pwd,
	                                strlen((char*)local_con_conf[iCount].usr_pwd));
	            }
	        }

	        //
	        // connectin to the broker
	        //
	        if((sl_ExtLib_MqttClientConnect((void*)local_con_conf[iCount].clt_ctx,
	                            local_con_conf[iCount].is_clean,
	                            local_con_conf[iCount].keep_alive_time) & 0xFF) != 0)
	        {
	            UART_PRINT("\n\rBroker connect fail for conn no. %d \n\r",iCount+1);

	            //delete the context for this connection
	            sl_ExtLib_MqttClientCtxDelete(local_con_conf[iCount].clt_ctx);

	            break;
	        }
	        else
	        {
	            UART_PRINT("\n\rSuccess: conn to Broker no. %d\n\r ", iCount+1);
	            local_con_conf[iCount].is_connected = true;
	            iConnBroker++;
	        }

	        //
	        // Subscribe to topics
	        //

	        if(sl_ExtLib_MqttClientSub((void*)local_con_conf[iCount].clt_ctx,
	                                   local_con_conf[iCount].topic,
	                                   local_con_conf[iCount].qos, TOPIC_COUNT) < 0)
	        {
	            UART_PRINT("\n\r Subscription Error for conn no. %d\n\r", iCount+1);
	            UART_PRINT("Disconnecting from the broker\r\n");
	            sl_ExtLib_MqttClientDisconnect(local_con_conf[iCount].clt_ctx);
	            local_con_conf[iCount].is_connected = false;

	            //delete the context for this connection
	            sl_ExtLib_MqttClientCtxDelete(local_con_conf[iCount].clt_ctx);
	            iConnBroker--;
	            break;
	        }
	        else
	        {
	        	int iSub;
	        	UART_PRINT("Client subscribed on following topics:\n\r");
	        	for(iSub = 0; iSub < local_con_conf[iCount].num_topics; iSub++)
	        	{
	        		UART_PRINT("%s\n\r", local_con_conf[iCount].topic[iSub]);
	        	}
	        }
	        iCount++;
	    }

	    if(iConnBroker < 1)
	    {
	    	//
	    	// no succesful connection to broker
	    	//
	    	goto end;
	    }

	    iCount = 0;

	    for(;;)
	    {
	    	osi_MsgQRead( &g_PBQueue, &RecvQue, OSI_WAIT_FOREVER);

	    	if(READ_TEMP == RecvQue)
	    	{
	    	    TMP006DrvGetTemp(&temp);
	    	    struct json_out out1 = JSON_OUT_BUF(json_buffer, sizeof(json_buffer));

	    	    //Reinicio out1, de lo contrario se van acumulando los printfs

	    	    json_printf(&out1,"{ Temperature : %f }",roundf(temp));


	    	    sl_ExtLib_MqttClientSend((void*)local_con_conf[iCount].clt_ctx,
	    	            pub_topic_temp,json_buffer,strlen((char*)json_buffer),QOS2,RETAIN);


	    	    UART_PRINT("\n\r CC3200 Publishes the following message \n\r");
	    	    UART_PRINT("Topic: %s\n\r",pub_topic_temp);
	    	}
	    	else if(READ_ACC == RecvQue)
	    	{
	    	    BMA222ReadNew(&accX, &accY, &accZ);
	    	    struct json_out out1 = JSON_OUT_BUF(json_buffer, sizeof(json_buffer));

	    	    //Reinicio out1, de lo contrario se van acumulando los printfs

	    	    json_printf(&out1,"{ AccX : %d, AccY: %d, AccZ : %d }", (int)accX, (int)accY, (int)accZ);


	    	    sl_ExtLib_MqttClientSend((void*)local_con_conf[iCount].clt_ctx,
	    	            pub_topic_acc,json_buffer,strlen((char*)json_buffer),QOS2,RETAIN);


	    	    UART_PRINT("\n\r CC3200 Publishes the following message \n\r");
	    	    UART_PRINT("Topic: %s\n\r",pub_topic_acc);
	    	}
	    	else if(BROKER_DISCONNECTION == RecvQue)
	    	{
	    		iConnBroker--;
	    		if(iConnBroker < 1)
	    		{
	    			//
					// device not connected to any broker
	    			//
	    			goto end;
	    		}
	    	}
	    }
	    end:
		//
		// Deinitializating the client library
		//
		sl_ExtLib_MqttClientExit();
		UART_PRINT("\n\r Exiting the Application\n\r");

		//LOOP_FOREVER();
		//Kill the task
		OsiTaskHandle handle=NULL;
		osi_TaskDelete(&handle);


}
#endif





//*****************************************************************************
//
//! Task implementing MQTT client communication to other web client through
//!    a broker
//!
//! \param  none
//!
//! This function
//!    1. Initializes network driver and connects to the default AP
//!    2. Initializes the mqtt library and set up MQTT connection configurations
//!    3. set up the button events and their callbacks(for publishing)
//!    4. handles the callback signals
//!
//! \return None
//!
//*****************************************************************************
void ConnectWiFI(void *pvParameters)
{
    
    long lRetVal = -1;
#ifdef STATION_MODE
    //These are only used in that mode
    int iCount = 0;
    int iNumBroker = 0;
    int iConnBroker = 0;
    osi_messages RecvQue;
    connect_config *local_con_conf = (connect_config *)app_hndl;
#endif
    //
    // Configure LED
    //
    GPIO_IF_LedConfigure(LED1|LED2|LED3);

    GPIO_IF_LedOff(MCU_RED_LED_GPIO);
    GPIO_IF_LedOff(MCU_GREEN_LED_GPIO);

    //
    // Reset The state of the machine
    //
    Network_IF_ResetMCUStateMachine();

    //
    // Start the driver
    //
#ifdef STATION_MODE
    lRetVal = Network_IF_InitDriver(ROLE_STA);
    if(lRetVal < 0)
    {
       UART_PRINT("Failed to start SimpleLink Device\n\r",lRetVal);
       LOOP_FOREVER();
    }

    // switch on Green LED to indicate Simplelink is properly up
    GPIO_IF_LedOn(MCU_ON_IND);

    // Start Timer to blink Red LED till AP connection
    LedTimerConfigNStart();

    // Initialize AP security params
    SecurityParams.Key = (signed char *)SECURITY_KEY;
    SecurityParams.KeyLen = strlen(SECURITY_KEY);
    SecurityParams.Type = SECURITY_TYPE;

    //
    // Connect to the Access Point
    //
    lRetVal = Network_IF_ConnectAP(SSID_NAME, SecurityParams);
    if(lRetVal < 0)
    {
       UART_PRINT("Connection to an AP failed\n\r");
       LOOP_FOREVER();
    }
#else
    lRetVal = Network_IF_InitDriver(ROLE_AP);
    if (lRetVal<0)
    	LOOP_FOREVER();

    // switch on Green LED to indicate Simplelink is properly up
    GPIO_IF_LedOn(MCU_ON_IND);

    // Start Timer to blink Red LED
    LedTimerConfigNStart();

    lRetVal=sl_WlanSet(SL_WLAN_CFG_AP_ID, WLAN_AP_OPT_SSID, strlen(SSID_NAME), SSID_NAME); //Configura el SSID en modo AP...
    if (lRetVal<0)
        	LOOP_FOREVER();

    uint8_t secType=SECURITY_TYPE;
    lRetVal = sl_WlanSet(SL_WLAN_CFG_AP_ID,
    		WLAN_AP_OPT_SECURITY_TYPE, 1,
			(unsigned char *)&secType);
    if (lRetVal<0) ERR_PRINT( lRetVal);

    lRetVal = sl_WlanSet(SL_WLAN_CFG_AP_ID,
    		WLAN_AP_OPT_PASSWORD,
			strlen((const char *)SECURITY_KEY),
			(unsigned char *)SECURITY_KEY);
    if (lRetVal<0) ERR_PRINT( lRetVal);

    //Restart the network after changing access point name....
    lRetVal = sl_Stop(0xFF);
    if (lRetVal<0) ERR_PRINT( lRetVal);
    lRetVal = sl_Start(0, 0, 0);
    if (lRetVal<0) ERR_PRINT( lRetVal);

#endif

    //
    // Disable the LED blinking Timer as Device is connected to AP
    //
    LedTimerDeinitStop();

    //
    // Switch ON RED LED to indicate that Device acquired an IP
    //
    GPIO_IF_LedOn(MCU_IP_ALLOC_IND);

    //UtilsDelay(20000000);
    osi_Sleep(1000); //JMCG mas elegante una espera de RTOS

    GPIO_IF_LedOff(MCU_RED_LED_GPIO);
    GPIO_IF_LedOff(MCU_ORANGE_LED_GPIO);
    GPIO_IF_LedOff(MCU_GREEN_LED_GPIO);


    //
    // Inicializa la biblioteca que gestiona el bus I2C. (movido desde main(...))
    //
    if(I2C_IF_Open(I2C_MASTER_MODE_FST) < 0)
    {
   	 while (1);
    }

    osi_Sleep(10); //Espera un poco


    //Esto tiene que ser realizado en una tarea. De momento lo pongo aqui
    //Init Temperature Sensor
    if(TMP006DrvOpen() < 0)
    {
    	UARTprintf("TMP006 open error\n");
    }

    //Esto tiene que ser realizado en una tarea. De momento lo pongo aqui
    //Init Accelerometer Sensor
    if(BMA222Open() < 0)
    {
    	UARTprintf("BMA222 open error\n");
    }


    osi_Sleep(10); //Espera un poco

    //
    // Vuelve a poner los pines como salida GPIO
    // No deberia hacerlo mientras haya una transferencia I2C activa.
    /*MAP_PinTypeGPIO(PIN_01, PIN_MODE_0, false);
    MAP_GPIODirModeSet(GPIOA1_BASE, 0x4, GPIO_DIR_MODE_OUT);
    MAP_PinTypeGPIO(PIN_02, PIN_MODE_0, false);
    MAP_GPIODirModeSet(GPIOA1_BASE, 0x8, GPIO_DIR_MODE_OUT);*/

    //This disconfigures PIN1 and 2 for SENSORS...
    // Configure PIN_01 for I2C0 I2C_SCL
    //
    MAP_PinTypeI2C(PIN_01, PIN_MODE_1);

    //
    // Configure PIN_02 for I2C0 I2C_SDA
    //
    MAP_PinTypeI2C(PIN_02, PIN_MODE_1);

    //HAbilitamos los timers en modo PWM (pero NO habilitamos los pines como PWM)
    PWM_IF_Init(0);


    //
    // Display Application Banner
    //
    DisplayBanner("MQTT_Client");
    
    //Lanza el interprete de comandos...
    lRetVal = osi_TaskCreate(vUARTTask,
                        (const signed char *)"CmdLine",
                        OSI_STACK_SIZE, NULL, 2, NULL );

	if(lRetVal < 0)
	{
    	ERR_PRINT(lRetVal);
    	LOOP_FOREVER();
	}


#ifdef STATION_MODE
    //
    // Initialze MQTT client lib
    //
    lRetVal = sl_ExtLib_MqttClientInit(&Mqtt_Client);
    if(lRetVal != 0)
    {
        // lib initialization failed
        UART_PRINT("MQTT Client lib initialization failed\n\r");
        LOOP_FOREVER();
    }
    
    /******************* connection to the broker ***************************/
    iNumBroker = sizeof(usr_connect_config)/sizeof(connect_config);
    if(iNumBroker > MAX_BROKER_CONN)
    {
        UART_PRINT("Num of brokers are more then max num of brokers\n\r");
        LOOP_FOREVER();
    }

    while(iCount < iNumBroker)
    {
        //create client context
        local_con_conf[iCount].clt_ctx =
        sl_ExtLib_MqttClientCtxCreate(&local_con_conf[iCount].broker_config,
                                      &local_con_conf[iCount].CallBAcks,
                                      &(local_con_conf[iCount]));

        //
        // Set Client ID
        //
        sl_ExtLib_MqttClientSet((void*)local_con_conf[iCount].clt_ctx,
                            SL_MQTT_PARAM_CLIENT_ID,
                            local_con_conf[iCount].client_id,
                            strlen((char*)(local_con_conf[iCount].client_id)));

        //
        // Set will Params
        //
        if(local_con_conf[iCount].will_params.will_topic != NULL)
        {
            sl_ExtLib_MqttClientSet((void*)local_con_conf[iCount].clt_ctx,
                                    SL_MQTT_PARAM_WILL_PARAM,
                                    &(local_con_conf[iCount].will_params),
                                    sizeof(SlMqttWill_t));
        }

        //
        // setting username and password
        //
        if(local_con_conf[iCount].usr_name != NULL)
        {
            sl_ExtLib_MqttClientSet((void*)local_con_conf[iCount].clt_ctx,
                                SL_MQTT_PARAM_USER_NAME,
                                local_con_conf[iCount].usr_name,
                                strlen((char*)local_con_conf[iCount].usr_name));

            if(local_con_conf[iCount].usr_pwd != NULL)
            {
                sl_ExtLib_MqttClientSet((void*)local_con_conf[iCount].clt_ctx,
                                SL_MQTT_PARAM_PASS_WORD,
                                local_con_conf[iCount].usr_pwd,
                                strlen((char*)local_con_conf[iCount].usr_pwd));
            }
        }

        //
        // connectin to the broker
        //
        if((sl_ExtLib_MqttClientConnect((void*)local_con_conf[iCount].clt_ctx,
                            local_con_conf[iCount].is_clean,
                            local_con_conf[iCount].keep_alive_time) & 0xFF) != 0)
        {
            UART_PRINT("\n\rBroker connect fail for conn no. %d \n\r",iCount+1);
            
            //delete the context for this connection
            sl_ExtLib_MqttClientCtxDelete(local_con_conf[iCount].clt_ctx);
            
            break;
        }
        else
        {
            UART_PRINT("\n\rSuccess: conn to Broker no. %d\n\r ", iCount+1);
            local_con_conf[iCount].is_connected = true;
            iConnBroker++;
        }

        //
        // Subscribe to topics
        //

        if(sl_ExtLib_MqttClientSub((void*)local_con_conf[iCount].clt_ctx,
                                   local_con_conf[iCount].topic,
                                   local_con_conf[iCount].qos, TOPIC_COUNT) < 0)
        {
            UART_PRINT("\n\r Subscription Error for conn no. %d\n\r", iCount+1);
            UART_PRINT("Disconnecting from the broker\r\n");
            sl_ExtLib_MqttClientDisconnect(local_con_conf[iCount].clt_ctx);
            local_con_conf[iCount].is_connected = false;
            
            //delete the context for this connection
            sl_ExtLib_MqttClientCtxDelete(local_con_conf[iCount].clt_ctx);
            iConnBroker--;
            break;
        }
        else
        {
            int iSub;
            UART_PRINT("Client subscribed on following topics:\n\r");
            for(iSub = 0; iSub < local_con_conf[iCount].num_topics; iSub++)
            {
                UART_PRINT("%s\n\r", local_con_conf[iCount].topic[iSub]);
            }
        }
        iCount++;
    }

    if(iConnBroker < 1)
    {
        //
        // no succesful connection to broker
        //
        goto end;
    }

    iCount = 0;

    for(;;)
    {
        osi_MsgQRead( &g_PBQueue, &RecvQue, OSI_WAIT_FOREVER);
        
        if(READ_TEMP == RecvQue)
        {
        	TMP006DrvGetTemp(&temp);
        	struct json_out out1 = JSON_OUT_BUF(json_buffer, sizeof(json_buffer));

        	//Reinicio out1, de lo contrario se van acumulando los printfs

        	json_printf(&out1,"{ Temperature : %f }",roundf(temp));


        	sl_ExtLib_MqttClientSend((void*)local_con_conf[iCount].clt_ctx,
        			pub_topic_temp,json_buffer,strlen((char*)json_buffer),QOS2,RETAIN);


        	UART_PRINT("\n\r CC3200 Publishes the following message \n\r");
        	UART_PRINT("Topic: %s\n\r",pub_topic_temp);
        }
        else if(READ_ACC == RecvQue)
        {
        	BMA222ReadNew(&accX, &accY, &accZ);
        	struct json_out out1 = JSON_OUT_BUF(json_buffer, sizeof(json_buffer));

        	//Reinicio out1, de lo contrario se van acumulando los printfs

        	json_printf(&out1,"{ AccX : %d, AccY: %d, AccZ : %d }", (int)accX, (int)accY, (int)accZ);


        	sl_ExtLib_MqttClientSend((void*)local_con_conf[iCount].clt_ctx,
        			pub_topic_acc,json_buffer,strlen((char*)json_buffer),QOS2,RETAIN);


        	UART_PRINT("\n\r CC3200 Publishes the following message \n\r");
        	UART_PRINT("Topic: %s\n\r",pub_topic_acc);
        }
        else if(BROKER_DISCONNECTION == RecvQue)
        {
            iConnBroker--;
            if(iConnBroker < 1)
            {
                //
                // device not connected to any broker
                //
                goto end;
            }
        }
    }
end:
    //
    // Deinitializating the client library
    //
    sl_ExtLib_MqttClientExit();
    UART_PRINT("\n\r Exiting the Application\n\r");
    
    LOOP_FOREVER();


#else


    //Antes de salir, crea la tarea que genera el patron
    // Podría hacerlo en otro sitio, por ejemplo un comando.
	//lRetVal = osi_TaskCreate(ColorLedTask,
	//                        (const signed char *)"ColorLeds",
	//                        OSI_STACK_SIZE, NULL, 2, NULL );

	if(lRetVal < 0)
	{
	   	ERR_PRINT(lRetVal);
	   	LOOP_FOREVER();
	}




    //Cambiar....> Ahora la parque que se intenta conectar, se harÃ­a en el interprete de comandos...
    //Se tendria que lanzar una tarea adicional
    OsiTaskHandle handle=NULL;
    osi_TaskDelete(&handle);

#endif
}


void TempTask(void *pvParameters)
{
	int refresh;
	while(1){
		refresh = *((int *)pvParameters);
		osi_messages var = READ_TEMP;
		//
		// write message indicating exit from sending loop
		//
		osi_MsgQWrite(&g_PBQueue,&var,OSI_NO_WAIT);
		osi_Sleep(refresh);
	}
}

void AccTask(void *pvParameters)
{
    int refresh;
    while(1){
        refresh = *((int *)pvParameters);
        osi_messages var = READ_ACC;
        //
        // write message indicating exit from sending loop
        //
        osi_MsgQWrite(&g_PBQueue,&var,OSI_NO_WAIT);
        osi_Sleep(refresh);
    }
}

void SubTask()
{
    osi_Sleep(2000);
    char *topic[1] = {""};
    if (topic_leds)
    {
        strcpy(topic[0], topic_root);
        strcat(topic[0], sub_topic_leds);
        strcpy(sub_topic_leds, topic[0]);
        if(sl_ExtLib_MqttClientSub((void*)usr_connect_config[0].clt_ctx,
                topic, usr_connect_config[0].qos, 1) < 0)
        {
            UART_PRINT("\n Sub Topic Leds");
        }
    }
    if (topic_sensors)
    {
        strcpy(topic[0], topic_root);
        strcat(topic[0], sub_topic_sensors);
        strcpy(sub_topic_sensors, topic[0]);
        if(sl_ExtLib_MqttClientSub((void*)usr_connect_config[0].clt_ctx,
                topic, usr_connect_config[0].qos, 1) < 0)
        {
            UART_PRINT("\n Sub Topic Sensors");
        }
    }
    if (topic_temp)
    {
        strcpy(topic[0], topic_root);
        strcat(topic[0], pub_topic_temp);
        strcpy(pub_topic_temp, topic[0]);
    }
    if (topic_acc)
    {
        strcpy(topic[0], topic_root);
        strcat(topic[0], pub_topic_acc);
        strcpy(pub_topic_acc, topic[0]);
    }
}


//}

//*****************************************************************************
//
//! Main 
//!
//! \param  none
//!
//! This function
//!    1. Invokes the SLHost task
//!    2. Invokes the ConnectWiFI
//!
//! \return None
//!
//*****************************************************************************



void main()
{ 
    long lRetVal = -1;

    //
    // Initialize the board configurations
    //
    BoardInit();

    //
    // Pinmux for UART
    //
    PinMuxConfig();

    //
    // Configuring UART
    //
    InitTerm();

    InitRGBSPI();
    WSArrayInit(pui8SPIOut, sizeof(pui8SPIOut));
    InitSPITransfer((uint8_t*)pui8SPIOut, sizeof(pui8SPIOut));

    //
	// Start the SimpleLink Host
    //
    lRetVal = VStartSimpleLinkSpawnTask(SPAWN_TASK_PRIORITY);
    if(lRetVal < 0)
    {
        ERR_PRINT(lRetVal);
        LOOP_FOREVER();
    }

    //
    // Start the MQTT Client task
    //
    osi_MsgQCreate(&g_PBQueue,"PBQueue",sizeof(osi_messages),10);
    lRetVal = osi_TaskCreate(ConnectWiFI,
                            (const signed char *)"WifiApp",
                            OSI_STACK_SIZE, NULL, 2, NULL );

    if(lRetVal < 0)
    {
    	ERR_PRINT(lRetVal);
    	LOOP_FOREVER();
    }

    /*lRetVal = osi_TaskCreate(TempTask,
    		(const signed char *)"TempTask",
			OSI_STACK_SIZE, NULL, 2, NULL );

    if(lRetVal < 0)
    {
    	ERR_PRINT(lRetVal);
    	LOOP_FOREVER();
    }*/

    //
    // Start the task scheduler
    //
    osi_start();
}

