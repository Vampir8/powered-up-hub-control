#include "core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bridge_t b;
static void setup(void) {
    config_t c; config_defaults(&c);
    for (int i=0;i<HUBS;i++) {
        snprintf(c.hubs[i].address,18,"90:84:2B:00:00:%02X",i);
        c.trains[i].ramp_ms=0;
    }
    core_init(&b,&c);
    b.wifi=b.mqtt=b.subscribed=true;
    for(int i=0;i<HUBS;i++) b.hubs[i].connected=b.hubs[i].ready=b.hubs[i].enabled=true;
}
static void receive(const char *s,uint32_t now) {core_receive(&b,(const uint8_t*)s,strlen(s),false,now);}
static int next(int i,uint32_t now) {
    uint8_t f[8];port_state_t *p=NULL;
    assert(core_next(&b,i,now,f,&p));core_sent(p,f,now);return (int8_t)f[7];
}
int main(void) {
    setup(); command_t cmd;
    assert(b.config.mqtt_enabled);
    b.config.mqtt_enabled=false;
    receive("T|Train 1|p,50",1000);
    assert(b.ignored==1 && !b.trains[0].driving && b.trains[0].power==0);
    setup();
    receive("T|Train 1|p,50",1000);receive("T|Train 2|p,60",1000);
    core_stop_train(&b,0);
    assert(!b.trains[0].driving && b.trains[1].driving);
    assert(core_port(&b.hubs[0],0,false)->target==0);
    assert(core_port(&b.hubs[1],0,false)->target==60);
    setup();
    const char *bad[]={"T|Train 1|p,-1","T|Train 1|p,101","T|Train 1|p,01","T|Train 1|p,50\n","T|Other|p,50","T|Train 1|l,2","T|Train 1|a,2","T|Train 1|b,6","T|Train 1|p,1|x"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(!parse_command(&b.config,(const uint8_t*)bad[i],strlen(bad[i]),&cmd));
    const uint8_t nul[]={'T','|','T','r','a','i','n',' ','1','|','p',',','1',0};
    assert(!parse_command(&b.config,nul,sizeof(nul),&cmd));
    const uint8_t invalid[]={0xc0,0x80};assert(!utf8_valid(invalid,2));
    strcpy(b.config.trains[0].label,"\xc5\x81\xc3\xb3\x64\xc5\xba");
    const char *utf="T|\xc5\x81\xc3\xb3\x64\xc5\xba|p,50";
    assert(parse_command(&b.config,(const uint8_t*)utf,strlen(utf),&cmd));
    setup();
    // Eight outputs without detected motors, ARM, or synchronization confirmation.
    for(int i=0;i<8;i++) {
        char msg[50];snprintf(msg,sizeof(msg),"T|Train %d|p,50",i+1);receive(msg,1000);
        port_state_t *p=core_port(&b.hubs[i],0,false);assert(p&&!p->detected&&p->target==50);
        assert(next(i,1000)==50);
    }
    receive("T|Train 1|r",1100);assert(next(0,1100)==0);assert(next(0,1300)==-50);
    receive("T|Train 1|p,0",1400);assert(next(0,1400)==0);
    setup(); b.config.hubs[1].train=0;b.config.hubs[1].polarity[0]=-1;
    receive("T|Train 1|p,100",1000);
    assert(core_port(&b.hubs[0],0,false)->target==100);
    assert(core_port(&b.hubs[1],0,false)->target==-100);
    core_stop_all(&b);assert(core_port(&b.hubs[0],0,false)->target==0);
    receive("T|Train 1|f",1100);assert(core_port(&b.hubs[0],0,false)->target==0);
    receive("T|Train 1|p,1",1200);assert(core_port(&b.hubs[0],0,false)->target==1);
    core_receive(&b,(const uint8_t*)"T|Train 1|p,100",15,true,1300);
    assert(core_port(&b.hubs[0],0,false)->target==1);
    receive("T|Train 1|a,0",1350);assert(core_port(&b.hubs[0],0,false)->target==1);
    core_hub_lost(&b,0,42);assert(!b.trains[0].fresh);
    receive("T|Train 1|p,50",1400);assert(!core_port(&b.hubs[0],0,false));
    b.hubs[0].ready=b.hubs[0].connected=true;core_stop_train(&b,0);
    assert(next(0,1500)==0);receive("T|Train 1|p,50",1600);assert(next(0,1600)==50);
    setup();b.config.hubs[0].lamp=1;receive("T|Train 1|l,1",1000);
    assert(core_port(&b.hubs[0],1,false)->target==60);
    core_stop_all(&b);assert(core_port(&b.hubs[0],1,false)->target==0);
    setup(); b.config.trains[0].ramp_ms=1000;receive("T|Train 1|p,100",600000);
    assert(next(0,600100)==10);assert(next(0,600200)==20);
    receive("T|Train 1|r",600210);assert(next(0,600210)==0);assert(next(0,600460)==-10);
    setup();b.config.trains[0].brake=true;receive("T|Train 1|p,50",1000);assert(next(0,1000)==50);
    receive("T|Train 1|p,0",1100);assert(next(0,1100)==127);
    uint8_t f[8];port_state_t *p;assert(!core_next(&b,0,2000,f,&p));
    for(int gate=0;gate<5;gate++) {
        setup();if(gate==0)b.wifi=false;if(gate==1)b.mqtt=false;if(gate==2)b.subscribed=false;if(gate==3)b.fallback_ap=true;if(gate==4)b.maintenance=true;
        receive("T|Train 1|p,100",1000);assert(!core_port(&b.hubs[0],0,false));
    }
    setup();receive("T|Train 1|p,100",1000);core_sync(&b,1100);
    assert(!b.syncing&&b.sync_done&&core_port(&b.hubs[0],0,false)->target==0);
    receive("T|Train 1|p,50",1200);assert(core_port(&b.hubs[0],0,false)->target==50);
    uint8_t attach[]={15,0,4,0,1,2,0,0,0,0,0,0,0,0,0};core_lwp(&b,0,attach,sizeof(attach));
    assert(core_port(&b.hubs[0],0,false)->detected&&core_port(&b.hubs[0],0,false)->device==2);
    uint8_t long_frame[128]={0x80,1,0,0x45};core_lwp(&b,0,long_frame,sizeof(long_frame));assert(!b.errors);
    uint8_t combined[]={6,0,1,6,6,40,5,0,0x82,0,2};core_lwp(&b,0,combined,sizeof(combined));
    assert(b.hubs[0].battery==40&&core_port(&b.hubs[0],0,false)->feedback==2);
    setup();uint8_t led[10];b.config.hubs[0].led_rgb=0xff0080;
    assert(core_led_next(&b,0,1000,led));
    const uint8_t probe[]={6,0,0x22,50,1,0};
    assert(!memcmp(led,probe,6));core_led_sent(&b,0,1000,led);
    assert(!core_led_next(&b,0,1020,led));
    uint8_t wrong_name[]={9,0,0x44,50,1,0,'P','W','R'};
    core_lwp(&b,0,wrong_name,sizeof(wrong_name));
    assert(!core_port(&b.hubs[0],50,false));
    uint8_t rgb_name[]={11,0,0x44,50,1,0,'R','G','B',' ','O'};
    core_lwp(&b,0,rgb_name,sizeof(rgb_name));
    assert(core_port(&b.hubs[0],50,false)->device==0x17);
    assert(core_led_next(&b,0,1040,led)&&led[2]==0x41);
    uint8_t rgb_attach[]={15,0,4,50,1,23,0,0,0,0,0,0,0,0,0};
    core_lwp(&b,0,rgb_attach,sizeof(rgb_attach));
    assert(core_led_next(&b,0,1000,led));
    const uint8_t rgb_setup[]={10,0,0x41,50,1,1,0,0,0,1};
    assert(!memcmp(led,rgb_setup,10));core_led_sent(&b,0,1000,led);
    assert(b.hubs[0].led_sent_rgb==-2);
    assert(!core_led_next(&b,0,1020,led));
    uint8_t wrong_port[]={10,0,0x47,59,1,1,0,0,0,1};core_lwp(&b,0,wrong_port,10);
    assert(!b.hubs[0].led_mode_ready);
    uint8_t rgb_ack[]={10,0,0x47,50,1,1,0,0,0,1};core_lwp(&b,0,rgb_ack,10);
    assert(b.hubs[0].led_mode_ready&&core_led_next(&b,0,1040,led));
    const uint8_t rgb_expected[]={10,0,0x81,50,0x11,0x51,1,255,0,128};
    assert(!memcmp(led,rgb_expected,10));core_led_sent(&b,0,1040,led);
    assert(!core_led_next(&b,0,1060,led));
    uint8_t rgb_report[]={7,0,0x45,50,255,0,128};core_lwp(&b,0,rgb_report,7);
    assert(b.hubs[0].led_reported_rgb==0xff0080);
    b.config.hubs[0].led_rgb=0;assert(core_led_next(&b,0,1080,led));assert(led[2]==0x81&&!led[7]&&!led[8]&&!led[9]);
    core_stop_all(&b);assert(core_led_next(&b,0,1100,led));
    b.maintenance=true;assert(!core_led_next(&b,0,1120,led));b.maintenance=false;
    core_hub_lost(&b,0,0);assert(!core_led_next(&b,0,1200,led));
    b.hubs[0].ready=b.hubs[0].enabled=true;core_lwp(&b,0,rgb_attach,15);
    assert(core_led_next(&b,0,1220,led)&&led[2]==0x41);core_led_sent(&b,0,1220,led);
    assert(!core_led_next(&b,0,2000,led));
    assert(core_led_next(&b,0,2220,led));core_led_sent(&b,0,2220,led);
    assert(core_led_next(&b,0,3220,led));core_led_sent(&b,0,3220,led);
    assert(!core_led_next(&b,0,4220,led)&&b.hubs[0].led_error[0]);
    b.config.hubs[0].led_rgb=-1;assert(!core_led_next(&b,0,5000,led));
    setup();b.config.hubs[0].led_rgb=0x123456;
    for(int i=0;i<3;i++) {
        assert(core_led_next(&b,0,1000+i*1000,led)&&led[0]==6&&led[2]==0x22);
        core_led_sent(&b,0,1000+i*1000,led);
        assert(!core_led_next(&b,0,1020+i*1000,led));
    }
    assert(!core_led_next(&b,0,4000,led)&&b.hubs[0].led_error[0]);
    rgb_name[3]=0;core_lwp(&b,0,rgb_name,sizeof(rgb_name));
    assert(!core_port(&b.hubs[0],0,false));
    rgb_name[3]=50;core_lwp(&b,0,rgb_name,sizeof(rgb_name));
    assert(core_led_next(&b,0,4020,led)&&led[2]==0x41&&!b.hubs[0].led_error[0]);
    setup();
    for(int i=0;i<HUBS;i++) b.hubs[i].connected=false;
    bool visible[HUBS]={0};uint32_t advertised_at[HUBS]={0};
    assert(core_connect_candidate(&b,visible,advertised_at,10000,0)==-1);
    visible[7]=true;advertised_at[7]=10000;
    assert(core_connect_candidate(&b,visible,advertised_at,10000,0)==7);
    visible[0]=true;advertised_at[0]=10000;
    assert(core_connect_candidate(&b,visible,advertised_at,10000,1)==7);
    b.hubs[7].retry_at=11000;
    assert(core_connect_candidate(&b,visible,advertised_at,10000,1)==0);
    b.hubs[0].connected=true;
    assert(core_connect_candidate(&b,visible,advertised_at,10000,0)==-1);
    assert(core_connect_candidate(&b,visible,advertised_at,11000,0)==7);
    assert(core_connect_candidate(&b,visible,advertised_at,14000,0)==-1);
    advertised_at[7]=14000;b.hubs[7].enabled=false;
    assert(core_connect_candidate(&b,visible,advertised_at,14000,0)==-1);
    b.hubs[7].enabled=true;b.maintenance=true;
    assert(core_connect_candidate(&b,visible,advertised_at,14000,0)==-1);
    b.maintenance=false;advertised_at[7]=UINT32_MAX-10;b.hubs[7].retry_at=UINT32_MAX-5;
    assert(core_connect_candidate(&b,visible,advertised_at,10,0)==7);
    char error[128];strcpy(b.config.hubs[1].address,b.config.hubs[0].address);
    assert(!config_validate(&b.config,error,sizeof(error)));
    config_t ui;config_defaults(&ui);strcpy(ui.panel_password,"12345678");strcpy(ui.ap_password,"12345678");strcpy(ui.client_id,"test-ui");ui.trains[0].hidden=true;
    command_t ignored;
    assert(!parse_command(&ui,(const uint8_t*)"T|Train 1|p,10",14,&ignored));
    ui.hubs[0].hidden=true;ui.hubs[0].enabled=false;ui.hubs[0].train=-1;ui.hubs[0].motor[0]=-1;
    assert(config_validate(&ui,error,sizeof(error)));
    ui.hubs[0].enabled=true;assert(!config_validate(&ui,error,sizeof(error)));ui.hubs[0].enabled=false;
    strcpy(ui.trains[1].name,"Nazwa widoku");strcpy(ui.hubs[1].motor_name[0],"Silnik 1");assert(config_validate(&ui,error,sizeof(error)));
    strcpy(ui.trains[1].name,"Bad\nName");assert(!config_validate(&ui,error,sizeof(error)));
    puts("PASS: strict parser/UTF8, eight automatic outputs without detected devices, STOP/new power, retained, reconnect, lights, polarity, ramp/reversal, brake, network/maintenance gates, LWP, RGB discovery/setup/confirmation/retries/report/off/reconnect, advertising-based fair connection selection");return 0;
}
