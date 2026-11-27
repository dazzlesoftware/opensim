YOption allowall;
integer[] source; integer[] destination; integer[] source_alias; integer[] destination_alias;
integer order; integer source_count; integer start_count; integer destination_count; integer offset_count; integer count_count; integer after;
integer[] Source(){source_count++;order=order*10+1;return source;}
integer Start(){start_count++;order=order*10+2;return 0;}
integer[] Destination(){destination_count++;order=order*10+3;return destination;}
integer Offset(){offset_count++;order=order*10+4;return 0;}
integer Count(){count_count++;order=order*10+5;source=new integer[](1);source[0]=99;destination=new integer[](1);destination[0]=100;llSleep(0.1);return 1;}
default{state_entry(){source=new integer[](1);source[0]=11;destination=new integer[](1);source_alias=source;destination_alias=destination;xmrArrayCopy(Source(),Start(),Destination(),Offset(),Count());after=1;}}
