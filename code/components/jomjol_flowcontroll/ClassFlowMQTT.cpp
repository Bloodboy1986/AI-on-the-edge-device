#ifdef ENABLE_MQTT

#include <sstream>
#include <iomanip>
#include "ClassFlowMQTT.h"
#include "Helper.h"
#include "connect_wlan.h"
#include "ClassLogFile.h"

#include "time_sntp.h"
#include "interface_mqtt.h"
#include "ClassFlowPostProcessing.h"
#include "ClassFlowControll.h"

#include "server_mqtt.h"

#include <time.h>


#define __HIDE_PASSWORD

static const char *TAG = "MQTT";
#define LWT_TOPIC        "connection"
#define LWT_CONNECTED    "connected"
#define LWT_DISCONNECTED "connection lost"

extern const char* libfive_git_version(void);
extern const char* libfive_git_revision(void);
extern const char* libfive_git_branch(void);



//https://stackoverflow.com/questions/58018132/getting-an-image-as-base64-and-writing-as-jpg
static const char* B64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const std::string b64encode(const void* data, const size_t &len)
    {
        std::string result((len + 2) / 3 * 4, '=');
        char *p = (char*) data, *str = &result[0];
        size_t j = 0, pad = len % 3;
        const size_t last = len - pad;

        for (size_t i = 0; i < last; i += 3)
        {
            int n = int(p[i]) << 16 | int(p[i + 1]) << 8 | p[i + 2];
            str[j++] = B64chars[n >> 18];
            str[j++] = B64chars[n >> 12 & 0x3F];
            str[j++] = B64chars[n >> 6 & 0x3F];
            str[j++] = B64chars[n & 0x3F];
        }
        if (pad)  /// set padding
        {
            int n = --pad ? int(p[last]) << 8 | p[last + 1] : p[last];
            str[j++] = B64chars[pad ? n >> 10 & 0x3F : n >> 2];
            str[j++] = B64chars[pad ? n >> 4 & 0x03F : n << 4 & 0x3F];
            str[j++] = pad ? B64chars[n << 2 & 0x3F] : '=';
        }
        return result;
    }

std::string b64encode(const std::string& str)
{
    return b64encode(str.c_str(), str.size());
}



void ClassFlowMQTT::SetInitialParameter(void)
{
    uri = "";
    topic = "";
    topicError = "";
    topicRate = "";
    topicTimeStamp = "";
    maintopic = hostname;

    topicUptime = "";
    topicFreeMem = "";

    clientname = "AIOTED-" + getMac();

    OldValue = "";
    flowpostprocessing = NULL;  
    user = "";
    password = ""; 
    SetRetainFlag = 0;
    previousElement = NULL;
    ListFlowControll = NULL; 
    disabled = false;
    keepAlive = 25*60;
}       

ClassFlowMQTT::ClassFlowMQTT()
{
    SetInitialParameter();
}

ClassFlowMQTT::ClassFlowMQTT(std::vector<ClassFlow*>* lfc)
{
    SetInitialParameter();

    ListFlowControll = lfc;
    for (int i = 0; i < ListFlowControll->size(); ++i)
    {
        if (((*ListFlowControll)[i])->name().compare("ClassFlowPostProcessing") == 0)
        {
            flowpostprocessing = (ClassFlowPostProcessing*) (*ListFlowControll)[i];
        }
    }
}

ClassFlowMQTT::ClassFlowMQTT(std::vector<ClassFlow*>* lfc, ClassFlow *_prev)
{
    SetInitialParameter();

    previousElement = _prev;
    ListFlowControll = lfc;

    for (int i = 0; i < ListFlowControll->size(); ++i)
    {
        if (((*ListFlowControll)[i])->name().compare("ClassFlowPostProcessing") == 0)
        {
            flowpostprocessing = (ClassFlowPostProcessing*) (*ListFlowControll)[i];
        }
    }
}


bool ClassFlowMQTT::ReadParameter(FILE* pfile, string& aktparamgraph)
{
    std::vector<string> zerlegt;

    aktparamgraph = trim(aktparamgraph);

    if (aktparamgraph.size() == 0)
        if (!this->GetNextParagraph(pfile, aktparamgraph))
            return false;

    if (toUpper(aktparamgraph).compare("[MQTT]") != 0)       // Paragraph passt nich zu MakeImage
        return false;

    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph))
    {
        zerlegt = ZerlegeZeile(aktparamgraph);
        if ((toUpper(zerlegt[0]) == "USER") && (zerlegt.size() > 1))
        {
            this->user = zerlegt[1];
        }  
        if ((toUpper(zerlegt[0]) == "PASSWORD") && (zerlegt.size() > 1))
        {
            this->password = zerlegt[1];
        }               
        if ((toUpper(zerlegt[0]) == "URI") && (zerlegt.size() > 1))
        {
            this->uri = zerlegt[1];
        }
        if ((toUpper(zerlegt[0]) == "SETRETAINFLAG") && (zerlegt.size() > 1))
        {
            if (toUpper(zerlegt[1]) == "TRUE") {
                SetRetainFlag = 1;  
                setMqtt_Server_Retain(SetRetainFlag);
            }
        }
        if ((toUpper(zerlegt[0]) == "HOMEASSISTANTDISCOVERY") && (zerlegt.size() > 1))
        {
            if (toUpper(zerlegt[1]) == "TRUE")
                SetHomeassistantDiscoveryEnabled(true);  
        }
        if ((toUpper(zerlegt[0]) == "METERTYPE") && (zerlegt.size() > 1)) {
        /* Use meter type for the device class 
           Make sure it is a listed one on https://developers.home-assistant.io/docs/core/entity/sensor/#available-device-classes */
            if (toUpper(zerlegt[1]) == "WATER_M3") {
                mqttServer_setMeterType("water", "m³", "h", "m³/h");
            }
            else if (toUpper(zerlegt[1]) == "WATER_L") {
                mqttServer_setMeterType("water", "L", "h", "L/h");
            }
            else if (toUpper(zerlegt[1]) == "WATER_FT3") {
                mqttServer_setMeterType("water", "ft³", "m", "ft³/m"); // Minutes
            }
            else if (toUpper(zerlegt[1]) == "WATER_GAL") {
                mqttServer_setMeterType("water", "gal", "h", "gal/h");
            }
            else if (toUpper(zerlegt[1]) == "GAS_M3") {
                mqttServer_setMeterType("gas", "m³", "h", "m³/h");
            }
            else if (toUpper(zerlegt[1]) == "GAS_FT3") {
                mqttServer_setMeterType("gas", "ft³", "m", "ft³/m"); // Minutes
            }
            else if (toUpper(zerlegt[1]) == "ENERGY_WH") {
                mqttServer_setMeterType("energy", "Wh", "h", "W");
            }
            else if (toUpper(zerlegt[1]) == "ENERGY_KWH") {
                mqttServer_setMeterType("energy", "kWh", "h", "kW");
            }
            else if (toUpper(zerlegt[1]) == "ENERGY_MWH") {
                mqttServer_setMeterType("energy", "MWh", "h", "MW");
            }
        }

        if ((toUpper(zerlegt[0]) == "CLIENTID") && (zerlegt.size() > 1))
        {
            this->clientname = zerlegt[1];
        }

        if (((toUpper(zerlegt[0]) == "TOPIC") || (toUpper(zerlegt[0]) == "MAINTOPIC")) && (zerlegt.size() > 1))
        {
            maintopic = zerlegt[1];
            mqttServer_setMainTopic(maintopic);
        }
    }

    /* Note:
     * Originally, we started the MQTT client here.
     * How ever we need the interval parameter from the ClassFlowControll, but that only gets started later.
     * To work around this, we delay the start and trigger it from ClassFlowControll::ReadParameter() */

    return true;
}


string ClassFlowMQTT::GetMQTTMainTopic()
{
    return maintopic;
}


bool ClassFlowMQTT::Start(float AutoIntervall) {

//    printf("URI: %s, MAINTOPIC: %s", uri.c_str(), maintopic.c_str());

    if ((uri.length() == 0) || (maintopic.length() == 0)) 
    {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "MQTT not started because URI or Maintopic is not set. MQTT will be disabled.");
        MQTTdisable();
        return false;
    }

    roundInterval = AutoIntervall; // Minutes
    keepAlive = roundInterval * 60 * 2.5; // Seconds, make sure it is greater thatn 2 rounds!

    std::stringstream stream;
    stream << std::fixed << std::setprecision(1) << "Digitizer interval is " << roundInterval <<
            " minutes => setting MQTT LWT timeout to " << ((float)keepAlive/60) << " minutes.";
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, stream.str());

    mqttServer_setParameter(flowpostprocessing->GetNumbers(), keepAlive, roundInterval);

    MQTT_Configure(uri, clientname, user, password, maintopic, LWT_TOPIC, LWT_CONNECTED, LWT_DISCONNECTED,
            keepAlive, SetRetainFlag, (void *)&GotConnected);

    if (!MQTT_Init()) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Init at startup failed! Retry with next publish call");
        return false;
    }

    return true;
}


bool ClassFlowMQTT::doFlow(string zwtime)
{
    std::string result;
    std::string resulterror = "";
    std::string resultraw = "";
    std::string resultpre = "";
    std::string resultrate = ""; // Always Unit / Minute
    std::string resultRatePerTimeUnit = ""; // According to selection
    std::string resulttimestamp = "";
    std::string resultchangabs = "";
    string zw = "";
    string namenumber = "";

    publishSystemData();

    if (flowpostprocessing)
    {
        std::vector<NumberPost*>* NUMBERS = flowpostprocessing->GetNumbers();

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Publishing MQTT topics...");

        for (int i = 0; i < (*NUMBERS).size(); ++i)
        {
            result =  (*NUMBERS)[i]->ReturnValue;
            resultraw =  (*NUMBERS)[i]->ReturnRawValue;
            resultpre =  (*NUMBERS)[i]->ReturnPreValue;
            resulterror = (*NUMBERS)[i]->ErrorMessageText;
            resultrate = (*NUMBERS)[i]->ReturnRateValue; // Unit per minutes
            resultchangabs = (*NUMBERS)[i]->ReturnChangeAbsolute; // Units per round
            resulttimestamp = (*NUMBERS)[i]->timeStamp;

            namenumber = (*NUMBERS)[i]->name;
            if (namenumber == "default")
                namenumber = maintopic + "/";
            else
                namenumber = maintopic + "/" + namenumber + "/";


            if (result.length() > 0)   
                MQTTPublish(namenumber + "value", result, SetRetainFlag);

            if (resulterror.length() > 0)  
                MQTTPublish(namenumber + "error", resulterror, SetRetainFlag);

            if (resultrate.length() > 0) {
                MQTTPublish(namenumber + "rate", resultrate, SetRetainFlag);
                
                std::string resultRatePerTimeUnit;
                if (getTimeUnit() == "h") { // Need conversion to be per hour
                    resultRatePerTimeUnit = resultRatePerTimeUnit = to_string((*NUMBERS)[i]->FlowRateAct * 60); // per minutes => per hour
                }
                else { // Keep per minute
                    resultRatePerTimeUnit = resultrate;
                }
                MQTTPublish(namenumber + "rate_per_time_unit", resultRatePerTimeUnit, SetRetainFlag);
            }

            if (resultchangabs.length() > 0) {
                MQTTPublish(namenumber + "changeabsolut", resultchangabs, SetRetainFlag); // Legacy API
                MQTTPublish(namenumber + "rate_per_digitalization_round", resultchangabs, SetRetainFlag);
            }

            if (resultraw.length() > 0)   
                MQTTPublish(namenumber + "raw", resultraw, SetRetainFlag);

            if (resulttimestamp.length() > 0)
                MQTTPublish(namenumber + "timestamp", resulttimestamp, SetRetainFlag);

            std::string json = flowpostprocessing->getJsonFromNumber(i, "\n");
            MQTTPublish(namenumber + "json", json, SetRetainFlag);

            string base64Image;
            std::string allImageData = "";
            string imagePath = "/sdcard/img_tmp/rot.jpg";
            std::ifstream lastImage(imagePath, std::ios::in | std::ios::binary);
            if (lastImage.is_open()) {
                char ch;
                while (lastImage.get(ch)) {
                    allImageData += ch;
                }
            }
            base64Image = b64encode(allImageData);
            //printf ("imagePath %s", base64Image.c_str());
            lastImage.close();
            MQTTPublish(namenumber + "image", "data:image/jpg;base64,"+base64Image, SetRetainFlag);
        }
    }
    
    /* Disabled because this is no longer a use case */
    // else
    // {
    //     for (int i = 0; i < ListFlowControll->size(); ++i)
    //     {
    //         zw = (*ListFlowControll)[i]->getReadout();
    //         if (zw.length() > 0)
    //         {
    //             if (result.length() == 0)
    //                 result = zw;
    //             else
    //                 result = result + "\t" + zw;
    //         }
    //     }
    //     MQTTPublish(topic, result, SetRetainFlag);
    // }
    
    OldValue = result;
    
    return true;
}


#endif //ENABLE_MQTT