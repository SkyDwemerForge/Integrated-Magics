# Feature 02 — Cast "instantâneo" sem o ciclo de equip/unequip

**Tipo:** Feature · **Origem:** comparação com o Spell Hotbar 2

## Pedido
Um modo mais ágil para combate: apertar a hotkey e lançar a magia quase instantaneamente, sem esperar o equip/unequip.

## Relato
> "...how overall snappy it feels in the heat of combat due to not having to equip/unequip anything. Like you hit a button and flick a lightning bolt near instantly."

## Notas
- O Spell Hotbar 2 lança a magia diretamente (sem equipar), com as limitações citadas pelo próprio usuário: desalinhamento de projétil, mira, etc.
- Alternativas: melhorar a latência atual (skip equip + skip channeling) ou oferecer um modo opcional de "cast direto".

## Tarefas
- [ ] Medir a latência atual, do press até o spell fire, com skip equip + skip channeling.
- [ ] Avaliar a viabilidade de um modo de cast direto (ex.: `CastSpellImmediate`) e seus trade-offs.
