# Bug 01 — Incompatibilidade com Skip Equip Animations (SEA) > 1.0.6

**Tipo:** Bug · **Prioridade sugerida:** Alta · **Relatos:** 2

## Descrição
Versões do Skip Equip Animations posteriores à 1.0.6 quebram o cast via hotkey do Integrated Magic.

## Comportamento por versão do SEA
| Versão SEA | Resultado |
|---|---|
| 1.0.6 | Funciona normalmente (Patches no padrão) |
| 1.0.7 | Só funciona com "Require exclusive hotkey" desativado |
| 1.0.8 / 1.0.9 | Slot no HUD recebe anel vermelho e nada acontece |

## Relatos
> "Hey just want to say that the newer versions of skip equip animation are not compatible and needs fixing."

> "Looks like the changes made to Skip Equip Animations after v1.0.6 are interfering with casting spells through this mod. Tested both 1.0.8 and 1.0.9 (latest version available) with no changes to Integrated Magic's default "Patches" settings - pressing the designated hotkey resulted in the appropriate spell HUD slot getting a red ring around it & nothing happens. Version 1.0.7 required disabling the "Require exclusive hotkey" setting to be able to cast spells. Swapping in SEA 1.0.6 worked without any issues."

## Workaround atual
Usar SEA 1.0.6, ou não usar o SEA e desligar os patches de skip equip.

## Investigação
- [ ] Obter o changelog / fonte do SEA 1.0.7–1.0.9 e identificar o que mudou (variáveis de animação, hooks de input?).
- [ ] Verificar se o SEA 1.0.7+ injeta/consome eventos de input que interferem com `requireExclusiveHotkeyPatch` (possível relação com [bug-02](bug-02-hotkeys-roda-mouse-exclusive.md)).
- [ ] Reproduzir com SEA 1.0.9 e build DEBUG; analisar logs `[FLOW]` / `[State]` quando o anel vermelho aparece.

## Arquivos prováveis
- `include/Adapters/Outbound/MagicEquip.h` / `src/Adapters/Outbound/MagicEquip.cpp` (`SetSkipEquipVars`, `ApplySkipEquipAnimReturn`)
- `include/Input/ExclusiveStore.h`, `src/Input/ExclusiveTracker.cpp`
