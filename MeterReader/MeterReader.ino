// Arduino Meter Reader.
// 
// Copyright (C) 2017 - Matt Brown
//
// All rights reserved.
//
// Counts blinks of an LDR attached to a EDMI mk7c smart meter.
//
// Also supports control of an attached relay for water valves.

#define D_PIN 2       // Pin connected to LDR voltage divider, used for wake-up
                      // interrupts.
#define L_PIN 5       // Pin connected to LED.
#define BUS_PIN 6     // Pin connected to one-wire bus.
#define WATER_PIN 50  // Pin connected to water relay

unsigned int ldr_count = 0;
unsigned long report_time = 0;
unsigned long ldr_first_pulse = 0;
unsigned long ldr_last_pulse = 0;
volatile byte ldr_pulse = 0;

const float w_per_pulse = 1;
const unsigned long ms_per_hour = (60 * 60 * 1000UL);

void ldr_isr() {
  ldr_pulse = 1;
}

void setup() {
  pinMode(D_PIN, INPUT);
  pinMode(WATER_PIN, OUTPUT);
  digitalWrite(WATER_PIN, LOW);
  attachInterrupt(0, ldr_isr, RISING);
  Serial.begin(57600);
  Serial.println("Hi");
  report_time = millis();
}

void status() {
  int water = digitalRead(WATER_PIN);
  char buf[1024];
  sprintf(buf, "WATER %d", water);
  Serial.println(buf);
}

unsigned long ldr_delta;

void loop() {
  static unsigned int ldr_watts;
  unsigned long now = millis();

  if (now - ldr_last_pulse > 60000) {
    ldr_watts = 0;
  }

  if (now - report_time > 5000) {
    ldr_delta = ldr_last_pulse - ldr_first_pulse;
    if (ldr_delta && ldr_count) {
      ldr_watts = (ldr_count - 1) * w_per_pulse * ms_per_hour / ldr_delta;
      ldr_count = 0;
    }

    Serial.print(now);
    Serial.print(" LDR ");
    Serial.println(ldr_watts);
    report_time = now;
  }

  if (ldr_pulse == 1) {
    if (now - ldr_last_pulse <= 240) {
      // Ignore any pulse recorded within 241ms of the previous pulse, as 1wh
      // in 240ms is equivalent to 63A of current at 240V, 0.99PF, which is
      // more than the breaker on the phase, so clearly implausible - e.g. must
      // be a measurement error (false positive).
      Serial.print(now);
      Serial.print(" FP_PULSE ");
      Serial.print(ldr_delta);
      Serial.print(" ");
      Serial.println(ldr_last_pulse);
      // These seem to come in bursts (??) so sleep for half of whatever the
      // last delta was (e.g. assume power usage stayed relatively constant) to
      // give it time to pass. This loses accuracy (e.g. miss a legitimate
      // pulse), but that's better than recording a bogus huge reading.
      //delay(ldr_delta/2);
    } else {
      ldr_count++;

      ldr_last_pulse = now;
      if (ldr_count == 1) { // was reset
        ldr_first_pulse = ldr_last_pulse;
      }
      Serial.print(now);
      Serial.print(" PULSE ");
      Serial.println(ldr_first_pulse);
    }
    ldr_pulse = 0;
  }

	int did_water = 0;
  while (Serial.available() > 0) {
    int cmd = Serial.read();
    switch (cmd) {
      case '\n':
        // ignore newlines, used to flush line buffers.
        break;
			case 'S':
				status();
				break;
      case 'w':
        digitalWrite(WATER_PIN, LOW);
				did_water = 1;
        break;
      case 'W':
        digitalWrite(WATER_PIN, HIGH);
				did_water = 1;
        break;
    }
  }
	if (did_water == 1) {
		status();
	}
}

// Vim modeline
// vim: set ts=2 sw=2 sts=2 et:
