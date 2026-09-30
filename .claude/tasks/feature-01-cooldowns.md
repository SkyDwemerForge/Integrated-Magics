# Feature 01 — Cooldowns configuráveis por magia/slot

**Tipo:** Feature · **Origem:** comparação com o Spell Hotbar 2

## Pedido
Permitir definir um cooldown para uma magia ou slot, impedindo que ela seja lançada de novo antes do tempo configurado.

## Relato
> "I love being able to add cooldowns via Spell Hotbar2, pick how the slotted spells are cast..."

## Notas
- Já existe o `SlotCooldownTracker` para exibir cooldown no HUD. Dá para aproveitá-lo para cooldowns definidos pelo usuário.
- Onde guardar: por magia (`SpellSettingsDB`, global) ou por slot (`SaveSpellDB`)? Decidir.

## Tarefas
- [ ] Definir o escopo (por magia vs. por slot).
- [ ] Adicionar o campo de cooldown na config/persistência e na UI de configuração.
- [ ] Bloquear `OnSlotPressed` enquanto o slot estiver em cooldown e mostrar isso no anel do HUD.
