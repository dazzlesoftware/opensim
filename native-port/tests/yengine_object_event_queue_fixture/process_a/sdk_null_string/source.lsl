YOption arrays;
object input;object alias;object first;object second;
string text;list result;integer entries;integer complete;
integer before_same;integer after_same;integer first_global;integer second_global;integer first_empty;
string make(){return llGetSubString("xsame",1,-1);}
default{state_entry(){entries++;text=llComputeHash("payload","unsupported");input=text;alias=input;before_same=(input==alias);xmrEventEnqueue([21,text,text]);llSleep(0.1);
result=xmrEventDequeue(0.0,2097152,0,0,0);first=result[1];second=result[2];
after_same=(first==second);first_global=(first==input);second_global=(second==input);first_empty=(first==(object)"");complete=1;}
touch_start(integer n){}}
