int add(int a, int b){
  return a + b;
}

int doub(int a){
  return a + a;
}


int main()
{
  int cond = doub(2);
  if(cond > 3){
    return add(1,4);
  }
  
  return 0;
}