# Bug 06 — Gamepad: o primeiro press só equipa, o segundo lança

**Tipo:** Bug · **Prioridade sugerida:** Média · **Relatos:** 1

## Descrição
No gamepad, pressionar o combo uma vez apenas troca para a magia; é preciso pressionar de novo para lançar.

## Relato
> "I use a gamepad. Pressing the combination key only once will only switch to a spell, and I have to press the combination key again to cast the spell. Is this officially normal."

## Tarefas
- [ ] Pedir ao usuário: versão do mod, modo de ativação, equipamento atual (arma na esquerda?), armas sacadas/guardadas, patches ativos, se usa o SEA.
- [ ] Verificar a relação com o [bug-05](bug-05-arma-esquerda-sacada.md) (estado das armas) e o [bug-03](bug-03-gamepad-slots-5-plus.md) (input do gamepad).
- [ ] Reproduzir com gamepad nos modos Automatic e Press.

## Arquivos prováveis
- `src/Domain/MagicStateLifecycle.cpp` (`OnSlotPressed`)
- `src/Domain/MagicStatePump.cpp`
