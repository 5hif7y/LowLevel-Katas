
package main

import "core:fmt"
import sdl "vendor:sdl3"

main :: proc() {
  // Inicializar SDL3.
  if !sdl.Init({.VIDEO}) {
    fmt.eprintln("SDL_Init fallo:")
    fmt.eprintln(sdl.GetError())
    return
  }
  defer sdl.Quit()

  // Crear ventana.
  window := sdl.CreateWindow(
    "Odin + SDL3",
    800,
    600,
    {},
  )

  if window == nil {
    fmt.eprintln("SDL_CreateWindow fallo:")
    fmt.eprintln(sdl.GetError())
    return
  }
  defer sdl.DestroyWindow(window)

  // nil permite que SDL elija el backend disponible.
  renderer := sdl.CreateRenderer(window, nil)

  if renderer == nil {
    fmt.eprintln("SDL_CreateRenderer fallo:")
    fmt.eprintln(sdl.GetError())
    return
  }
  defer sdl.DestroyRenderer(renderer)

  // Información útil para la depuración.
  fmt.println("Backend SDL:", sdl.GetRendererName(renderer))

  // x, y, ancho y alto, en píxeles.
  rect := sdl.FRect{100, 100, 300, 180}

  running := true

  for running {
    // Procesar eventos.
    for event: sdl.Event; sdl.PollEvent(&event); /**/ {
      #partial switch event.type {
      case .QUIT, .WINDOW_CLOSE_REQUESTED:
        running = false
      }
    }

    // Limpiar la ventana con gris oscuro.
    _ = sdl.SetRenderDrawColor(renderer, 24, 26, 34, 255)
    _ = sdl.RenderClear(renderer)

    // Dibujar un rectángulo celeste.
    _ = sdl.SetRenderDrawColor(renderer, 0, 180, 255, 255)
    _ = sdl.RenderFillRect(renderer, &rect)

    // Presentar el frame.
    sdl.RenderPresent(renderer)
  }
}

