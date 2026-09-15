float distance;
long duration;

const int trigPin = 10;
const int echoPin = 11;

void setup() {
  pinMode( trigPin, OUTPUT);
  pinMode( echoPin, INPUT);

  Serial.begin(115200);
}

void loop() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin , HIGH);

  distance = duration * 0.034 / 2;

  Serial.print("Distance: ");
  Serial.println(distance);
  Serial.println(" cm");

  delay(500);
  // put your main code here, to run repeatedly:

}
