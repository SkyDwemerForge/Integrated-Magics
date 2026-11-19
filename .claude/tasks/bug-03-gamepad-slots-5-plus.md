# Bug 03 — Gamepad: slots 5+ não bloqueiam a ação padrão do jogo

**Tipo:** Bug · **Prioridade sugerida:** Alta · **Relatos:** 1

## Descrição
No gamepad, os slots 1–4 funcionam corretamente, mas a partir do slot 5 o jogo prioriza a ação padrão da tecla em vez de lançar a magia.

## Exemplo
- Slot 1: RT+B → magia lançada normalmente.
- Slot 5: RT+B → personagem pula, magia não é lançada.

## Relato
> "Gamepad user here, the mod works perfectly but only for the first 4 slots, when using slot 5 and above the controls priority get messed up [...] all slots above slot4 always prioritize the game default actions for your key presses"

## Hipótese
Algum array/loop de filtragem ou consumo de input (ou o cache de hotkeys de gamepad) está limitado a 4 slots em vez de `kMaxSlots`.

## Tarefas
- [ ] Procurar limites fixos em 4 (ou o tamanho de um array antigo) no caminho de filtragem de input de gamepad.
- [ ] Verificar `HotkeyCacheStore` e o filtro em `ProcessAndFilter` para slots ≥ 5.
- [ ] Testar com teclado também, para descartar que o problema seja geral.

## Arquivos prováveis
- `include/Input/HotkeyCacheStore.h`, `include/Input/HotkeyMatcher.h`
- `PollInputDevicesHook::ProcessAndFilter` em `src/Adapters/Inbound/`
- `include/Config/Limits.h`
