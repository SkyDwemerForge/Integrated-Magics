# Bug 04 — Cast da mão esquerda travado/interrompido (relação com bloqueio)

**Tipo:** Bug · **Prioridade sugerida:** Média · **Relatos:** 2 (possivelmente a mesma causa)

## Descrição
A mão esquerda entra num estado em que o cast fica "tremendo" e é interrompido repetidamente, ou o ciclo equip > cast > unequip acontece sem lançar nada.

## Observações dos usuários
- Ligado ao bloqueio (botão direito do mouse):
  - Segurar o bloqueio enquanto segura a hotkey de cast → entra no estado bugado.
  - Clicar o bloqueio algumas vezes rápido enquanto segura a hotkey → sai do estado bugado.
- No modo Automatic, é intermitente; não acontece ao definir um modo específico (Press/Hold) por magia.

## Relatos
> "it seems that the left hand cast issue is related to the block behavior which use Right Mouse Button. if you get stuck in the state that left hand casting twitching and interrupted, quick clicking few times block key while holding the casting hotkey will fix it. vice versa, if you're in normal state, holding block key and entering block state while holding the casting hotkey will make you stuck in left hand casting twitching and interrupted again."

> "Sometimes Under Automatic mode, LH casting will get into a state that goes through whole process of equip>cast>unequip but nothing casted. [...] It works well if you set specific mode(Press, Hold etc.) for each type spell."

## Hipótese
Um estado residual de bloqueio no behavior graph (ou um input de ataque esquerdo/bloqueio compartilhado) conflita com o input sintético da mão esquerda. O `OnCasterInterrupt` pode estar entrando em loop de restart.

## Tarefas
- [ ] Reproduzir: segurar bloqueio + hotkey de magia na mão esquerda; coletar log `[FLOW]`.
- [ ] Verificar se o input sintético de ataque esquerdo coincide com o evento de bloqueio.
- [ ] Checar se o loop de restart (`kRedispatchInterval`, `pendingRestartNextFrame`) tem saída quando o ator está em estado de bloqueio.
- [ ] Considerar limpar/forçar a saída do estado de bloqueio antes de iniciar o cast.

## Arquivos prováveis
- `src/Domain/MagicStatePump.cpp` (`PumpCastPhase`, `OnCasterInterrupt`, `PumpAutoAttack`)
- `include/Adapters/Outbound/SyntheticInput.h`
