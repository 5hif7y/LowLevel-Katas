package main

import "core:fmt"
import "core:math"

main :: proc (){
  // Problem data
  v0: f64 = 20.0
  angle_grades: f64 = 30.0
  g: f64 = 9.81

  // Covert angle to radians
  angle_rad := angle_grades * math.RAD_PER_DEG

  // Descomponer la velocidad inicial
  vx := v0 * math.cos(angle_rad)
  vy := v0 * math.sin(angle_rad)

  // Calculate movement magnitude
  fly_time := 2.0 * vy /g 
  max_time := vy /g 
  max_h := (vy * vy) / (2.0 * g)
  dist := vx * fly_time 

  // Print results
  fmt.println("=== --- ===\n")
  fmt.printf("Initial vel: %.3f m/s\n", v0)
  fmt.printf("Launch angle : %.1f grades\n", angle_grades)

  fmt.println("Componentes iniciales\n")
  fmt.println("Horizontal vel.: %.3f m/s\n", vx)
  fmt.println("Vertical vel: %.3f m/s\n", vy)
  
  fmt.println("Results\n")
  fmt.println("Time to max h: %.3f s\n", max_time)
  fmt.println("Total fly time: %.3f s\n", fly_time )
  fmt.println("Max h: %.3f m\n", max_h)
  fmt.println("Horizontal distance: %.3f m\n", dist)
  
}

