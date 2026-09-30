# Bug 07 — Crash ao pressionar H

**Tipo:** Bug · **Prioridade sugerida:** Alta (crash) · **Relatos:** 1

## Descrição
O jogo crasha toda vez que o usuário pressiona H. H provavelmente é o toggle do HUD/popup de atribuição.

## Relato
> "My game crashes everytime I press H with it"

## Tarefas
- [ ] Pedir o crash log (Crash Logger / .NET Script Framework), a versão do jogo (SE/AE/VR), a versão do mod e a load order.
- [ ] Verificar em que contexto acontece (fora de menu, ou com o Magic Menu aberto).
- [ ] Revisar o caminho do toggle do HUD: `PopupDrawer`, `HudManager`, `TextureManager` (texturas ausentes?).

## Arquivos prováveis
- `src/UI/PopupDrawer.cpp`, `src/UI/HudManager.cpp`
