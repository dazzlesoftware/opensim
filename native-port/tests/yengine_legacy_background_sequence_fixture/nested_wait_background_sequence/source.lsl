list result;list nested;integer phase;integer calls;
        integer effects;integer arguments;string seen;
        integer helper(integer n){effects++;llSleep(0.2);return n+1;}
        default{state_entry(){}
          touch_end(integer n){phase=1;result=xmrEventDequeue(10.0,536870912,0,2097152,0);arguments+=n;phase=2;}
          touch_start(integer n){calls++;seen=llDetectedName(0);arguments=n;
            nested=xmrEventDequeue(5.0,0,16,0,0);effects+=helper(n);}
          on_rez(integer n){} timer(){}}
    