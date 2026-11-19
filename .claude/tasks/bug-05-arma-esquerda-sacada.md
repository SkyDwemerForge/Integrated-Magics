# Bug 05 — Cast falha com arma/escudo na mão esquerda depois de sacar as armas (v1.5.1)

**Tipo:** Bug · **Prioridade sugerida:** Alta · **Relatos:** 1

## Descrição
Com arma ou escudo na mão esquerda (duas espadas, ou escudo + espada), o cast falha na maioria das vezes quando as armas estão sacadas.

## Comportamento
| Estado | Resultado |
|---|---|
| Armas guardadas | Cast funciona (mão esquerda, direita ou dual) |
| Armas sacadas | Falha na maioria das vezes |
| Guardar → cast → sacar → cast | O primeiro cast depois de sacar funciona; os seguintes falham |
| Magia nas duas mãos, ou magia na esquerda + espada na direita | Sempre funciona |

## Relato
> "When dual-wielding two swords, or holding a shield in the left hand and a sword in the right hand: Spells cast successfully [...] while weapons are sheathed. However, spellcasting fails most of the time after unsheathing weapons. If you first sheath your weapons to cast a spell successfully once, then unsheathe your weapons to cast again, the spell will definitely work on that attempt. All subsequent spellcasts will consistently fail afterward."

## Hipótese
Quando a mão esquerda tem arma/escudo e as armas estão sacadas, a troca para magia não leva o behavior graph ao estado de magia (ex.: falta de redraw / variáveis de equip), ou o restore deixa um estado inconsistente que afeta os casts seguintes.

## Tarefas
- [ ] Verificar se o commit `7819c49` (restore da mão esquerda com arma na outra mão) já resolve.
- [ ] Reproduzir com duas espadas e com escudo + espada, armas sacadas; comparar os logs do 1º cast (funciona) com os do 2º (falha).
- [ ] Revisar o snapshot/restore em `RestoreContext` para objetos da mão esquerda que não são magia.

## Arquivos prováveis
- `src/Domain/MagicStateLifecycle.cpp`
- `include/Adapters/Outbound/RestoreEquip.h`
- `src/Adapters/Outbound/MagicEquip.cpp`

## Relacionados
- [bug-06](bug-06-gamepad-duplo-press.md)
