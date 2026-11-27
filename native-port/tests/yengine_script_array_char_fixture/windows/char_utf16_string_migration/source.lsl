YOption allowall;
char[] input; char[] alias; string text; string copied; integer before_last; integer after_last; integer after;
default{state_entry(){input=new char[](4);input[0]=(char)55357;input[1]=(char)56832;input[2]=(char)0;input[3]=(char)55296;text=xmrChars2String(input,0,4);alias=input;before_last=(integer)text[3];input[0]=(char)0;input[1]=(char)0;input[2]=(char)0;input[3]=(char)0;llSleep(0.1);after_last=(integer)text[3];xmrString2Chars(text,0,input,0,4);copied=xmrChars2String(input,0,4);after=(alias==input);}}
