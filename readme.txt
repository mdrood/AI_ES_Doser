when i ota i grab wrongb reefDoser.
light apex selection doesn't work both intern changes and goes back


Kalkwasser -> CaCO2 -> Calcium Hydroxide
Calcium->CACl2->Calcium Chloride
Lye->NaOH->Alkalinity
Mg->Magnisiu 


in AI_Engine.h is where you change chemistry strength
    struct {
        //TODO this is where you change chemitry strength
        float dkhPerMlKalk = 0.04, dkhPerMlAfr = 0.1, dkhPerMlAlk = 0.15, dkhPerMlNaoh = 0.25, mgPerMlMg = 5.0, caPerMlCacl2 = 553.3;
    } chem;


for eric's tank vs mine
change potency of dosing agents.
changed emercy from 1 and 15 to 45 and 1500
