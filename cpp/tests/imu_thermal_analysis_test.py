#!/usr/bin/env python3
"""Numerical regression checks; generated signals are test fixtures only."""
import importlib.util
import unittest
from pathlib import Path

import numpy as np

SCRIPT=Path(__file__).resolve().parents[2]/'tools/analyze_dual_imu_thermal.py'
spec=importlib.util.spec_from_file_location('thermal',SCRIPT)
thermal=importlib.util.module_from_spec(spec)
spec.loader.exec_module(thermal)


class ThermalAnalysisTest(unittest.TestCase):
    def test_known_temperature_slopes_and_holdout(self):
        t=np.arange(2.5,1200,5)
        temperature=25+t/200
        expected=np.array([0.01,-0.02,0.03])
        y=np.array([0.5,-0.4,0.2])+temperature[:,None]*expected
        model,prediction=thermal.fit_thermal(t,temperature,y)
        np.testing.assert_allclose(model['slope_per_c'],expected,atol=1e-12)
        np.testing.assert_allclose(prediction,y,atol=1e-12)
        self.assertLess(max(model['holdout_linear_rmse']),1e-12)
        self.assertFalse(model['deployment_approved'])
        self.assertEqual(model['holdout_outside_training_temperature_blocks'],120)
        # Corrupt only the held-out half: training coefficients must not change.
        changed=y.copy();changed[t>=600]+=2
        other,_=thermal.fit_thermal(t,temperature,changed)
        np.testing.assert_array_equal(other['slope_per_c'],model['slope_per_c'])
        np.testing.assert_allclose(other['holdout_linear_mean_residual'],2,atol=1e-12)

    def test_no_temperature_variation(self):
        t=np.arange(2.5,1200,5)
        model,prediction=thermal.fit_thermal(t,np.full_like(t,30),np.ones((len(t),3)))
        self.assertFalse(model['fit_available']);self.assertIsNone(prediction)

    def test_small_span_is_flagged(self):
        t=np.arange(2.5,1200,5);temperature=30+t/12000
        model,_=thermal.fit_thermal(t,temperature,np.column_stack((temperature,)*3))
        self.assertFalse(model['training_span_sufficient_for_exploratory_fit'])
        self.assertFalse(model['deployment_approved'])

    def test_block_boundaries_and_unbiased_variance(self):
        t=np.array([0.,1.,4.9,5.,6.,9.9]);y=np.array([1.,2.,3.,4.,5.,6.])[:,None]
        blocks=thermal.block_stats(t,y,10)
        self.assertEqual([b['samples'] for b in blocks],[3,3])
        np.testing.assert_allclose([b['mean'][0] for b in blocks],[2,5])
        np.testing.assert_allclose([b['stddev'][0] for b in blocks],[1,1])
        with self.assertRaises(ValueError):thermal.block_stats(t[:3],y[:3],10)

    def test_integral_uses_actual_irregular_times(self):
        t=np.array([60.,60.1,60.7,62.,63.])
        rate=np.column_stack((np.ones(5)*2,-np.ones(5),t-60))
        result=thermal.integrate(t,rate)
        np.testing.assert_allclose(result[-1],[6,-3,4.5])
        np.testing.assert_array_equal(result[0],[0,0,0])


if __name__=='__main__':unittest.main()
