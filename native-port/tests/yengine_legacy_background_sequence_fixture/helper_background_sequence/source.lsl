integer phase;integer effects;list saved;list nested;list result;string observed;
        default{state_entry(){result=xmrEventDequeue(20.0,536870912,0,2097152,0);phase=2;}
          touch_start(integer n){effects++;saved=xmrEventSaveDets();xmrEventLoadDets([1]);
            xmrEventEnqueue([36,9]);nested=xmrEventDequeue(0.0,0,16,0,0);
            xmrEventLoadDets(saved);observed=llDetectedName(0);phase=1;llSleep(5.0);}
          touch_end(integer n){}on_rez(integer n){}}
    