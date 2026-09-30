# Bug 02 — Hotkeys param após usar a roda do mouse com "Require exclusive hotkey"

**Tipo:** Bug · **Prioridade sugerida:** Alta · **Relatos:** 2

## Descrição
Com `requireExclusiveHotkeyPatch` ativo, depois de qualquer evento da roda do mouse (zoom de câmera, scroll no QuickLoot IE), nenhuma hotkey de slot funciona até o jogador abrir um menu que pausa o jogo (ESC, Favoritos, inventário, magia).

## Passos para reproduzir
1. Ativar "Require exclusive hotkey" em Patches.
2. Configurar slots com hotkeys (ex.: teclas 1–4).
3. Usar a roda do mouse (zoom de câmera, ou scroll no QuickLoot com mais de 1 item).
4. Pressionar a hotkey do slot → nada acontece.

Setas do teclado no QuickLoot **não** causam o problema. Desativar o patch resolve.

## Relatos
> "I have bound spell hotkeys to the main‑row number keys 1‑2‑3‑4. After I adjust the camera view with the mouse scroll wheel, pressing those keys fails to cast the assigned spells. The hotkeys return to normal only after pressing ESC or Q (Favorites menu)."

> "Integrated Magic's keybinds stop working after using mouse wheel to scroll through items via QuickLoot. [...] this is specific to mouse wheel scrolling [...] Found the culprit - it's 'Require exclusive hotkey' setting in 'Patches' section."

## Hipótese
Os eventos da roda do mouse chegam como "press" sem um "release" correspondente. O código da roda fica marcado como pressionado no `KeyStateStore`, e a checagem exclusiva sempre vê uma tecla extra pressionada. Abrir um menu provavelmente reseta o estado das teclas.

## Tarefas
- [ ] Confirmar via log `[Input]` que o código da roda fica preso como "down".
- [ ] Ignorar os códigos da roda (wheel up/down) no `KeyStateStore`/checagem exclusiva, ou tratá-los como pulso (down+up no mesmo frame).
- [ ] Revisar se outros eventos sem release (ex.: eventos sintéticos de outros mods) podem causar o mesmo efeito.
- [ ] Verificar a relação com o sintoma do SEA 1.0.7 ([bug-01](bug-01-sea-incompatibilidade.md)).

## Arquivos prováveis
- `include/Input/KeyStateStore.h`
- `include/Input/ExclusiveStore.h`, `src/Input/ExclusiveTracker.cpp`
- `PollInputDevicesHook` em `src/Adapters/Inbound/`
