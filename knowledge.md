# MyLang Architecture Spec

## Goal
A simple, human-friendly programming language transpiled into C99.

## Target Syntax Example (games/app.vibes):
```vibes
window "My Game", 800, 600

on update {
    draw_circle 400, 300, 50, "red"
}