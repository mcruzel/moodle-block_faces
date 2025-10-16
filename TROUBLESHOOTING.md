# Résolution des erreurs « Impossible de trouver l’enregistrement de données »

Cette erreur apparaît lorsque le trombinoscope tente de charger un groupe ou un utilisateur qui n’existe plus dans la base, ou n’est plus accessible dans le cours. Voici les actions à mener pour l’isoler puis le corriger.

## 1. Activer le mode débogage et consulter la trace
1. Activez « *Mode développeur* » et l’affichage des messages de débogage dans l’administration du site (`Administration du site > Développement > Débogage`).
2. Reproduisez l’ouverture de `/blocks/faces/showfaces/show.php`. La trace complète indiquera quelle requête `get_record()` a échoué et quel identifiant est en cause.

## 2. Vérifier les paramètres transmis à la page
1. Sur l’URL fautive, notez les paramètres `cid`, `groupid` et `groupids[]`. Ce sont ceux lus par le contrôleur avant d’instancier la page (`showfaces/show.php`). 【F:showfaces/show.php†L27-L58】
2. Supprimez manuellement de l’URL tout identifiant suspect (par exemple un `groupid` d’un ancien groupe) et rechargez la page. Le bouton « Réinitialiser » de l’interface utilise la même logique pour effacer la sélection (`faces_page::export_for_template`). 【F:classes/output/faces_page.php†L162-L201】

## 3. Contrôler l’existence et la visibilité des groupes
1. Pour chaque identifiant collecté à l’étape 2, vérifiez dans la base `mdl_groups` qu’il existe toujours et que la colonne `courseid` correspond bien au cours (`SELECT id, courseid FROM mdl_groups WHERE id IN (...)`).
2. Si l’identifiant est absent ou associé à un autre cours, supprimez-le des préférences utilisateur ou recréez le groupe.
3. Vérifiez aussi que l’utilisateur dispose bien des permissions pour voir ce groupe (`groups_group_visible()` est appelé lors de la validation). 【F:classes/local/groups_helper.php†L41-L104】

## 4. Rafraîchir la sélection de groupes multiples
1. Si vous utilisez l’impression par regroupements, soumettez à nouveau le formulaire de sélection après avoir décoché les groupes obsolètes. La préparation des sections s’appuie sur cette sélection avant d’interroger les inscriptions (`faces_page::export_for_template`). 【F:classes/output/faces_page.php†L117-L159】
2. Répétez l’opération pour la version imprimable au besoin (`faces_print_page`). 【F:classes/output/faces_print_page.php†L52-L113】

## 5. Nettoyer les données historiques si nécessaire
1. Si le problème provient d’une entrée conservée dans les préférences utilisateur ou la table `mdl_user_preferences`, supprimez la clé `block_faces_groupids` (ou équivalente) pour les comptes concernés.
2. Après nettoyage, rechargez la page pour confirmer que l’erreur ne se produit plus.

En suivant ces étapes, vous identifierez l’identifiant cassé, le remplacerez ou le supprimerez, et les appels au trombinoscope cesseront de lever l’exception « Impossible de trouver l’enregistrement de données ».
