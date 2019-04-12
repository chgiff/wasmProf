int add(int a, int b){
  return a + b;
}

int doub(int a){
  return a + a;
}


int main()
{
  int num = 1;
  while(num < 10) num = doub(num);
  
  return num;
}