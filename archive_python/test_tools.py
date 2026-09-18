import copy,json,unittest
from pathlib import Path
from bench import validate,plan
from collect import parse
from video_measure import angle

class Tests(unittest.TestCase):
    def setUp(self):self.c=json.loads(Path(__file__).with_name('config.json').read_text(encoding='utf-8'))
    def test_plan(self):
        validate(self.c);p=list(plan(self.c));self.assertEqual(len(p),108)
        self.assertEqual(p[-1][1],1500)
        self.assertTrue(all(1445<=v<=1555 for _,v in p))
    def test_bounds(self):
        self.c['offsets_us']=[101]
        with self.assertRaises(ValueError):validate(self.c)
    def test_duplicate(self):
        self.c['channels'][1]=copy.deepcopy(self.c['channels'][0])
        with self.assertRaises(ValueError):validate(self.c)
    def test_nan(self):
        self.c['offsets_us']=[float('nan')]
        with self.assertRaises(ValueError):validate(self.c)
    def test_parse(self):
        r=parse('msh> SERVO,2,1,4,1522,333,10,11,0\r\n')
        self.assertEqual(r['pulse_us'],1522);self.assertNotIn('angle',r)
    def test_bad_record(self):
        with self.assertRaises(ValueError):parse('SERVO,1,1,8,1500,333,1,1,0')
    def test_noise(self):self.assertIsNone(parse('booting...'))
    def test_angles(self):
        self.assertAlmostEqual(angle((10,10),(20,10),(10,0)),90)
        self.assertAlmostEqual(angle((10,10),(20,10),(10,20)),-90)
    def test_degenerate(self):
        with self.assertRaises(ValueError):angle((0,0),(0,0),(20,10))
if __name__=='__main__':unittest.main()
