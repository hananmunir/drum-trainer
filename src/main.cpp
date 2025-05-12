#include <Arduino.h>

// put function declarations here:
int myFunction(int, int);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.println("Hello World");
  delay(1000); // wait for a second
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}