#!/usr/bin/env python3
"""
IEC 62304 Software Safety Classification Automation Tool

This module provides automated support for IEC 62304 software safety classification
based on hazard analysis and software contribution assessment.

Usage:
    python safety_classifier.py --config config.yaml --hazards hazards.json
    
Requirements:
    - Python 3.8+
    - PyYAML
    - jsonschema
    - logging
"""

import json
import yaml
import logging
import argparse
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
import jsonschema

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class SafetyClass(Enum):
    """IEC 62304 Software Safety Classifications"""
    CLASS_A = "Class_A"  # No contribution to hazardous situations
    CLASS_B = "Class_B"  # Non-life-threatening injury possible
    CLASS_C = "Class_C"  # Death or serious injury possible

class HarmSeverity(Enum):
    """Potential harm severity levels"""
    CATASTROPHIC = "Catastrophic"  # Death
    CRITICAL = "Critical"          # Serious injury
    SERIOUS = "Serious"            # Non-serious injury
    MINOR = "Minor"                # Temporary discomfort
    NEGLIGIBLE = "Negligible"      # No injury

class ContributionLevel(Enum):
    """Software contribution to hazard levels"""
    DIRECT = "Direct"         # Software directly causes hazard
    INDIRECT = "Indirect"     # Software contributes to hazard sequence
    REMOTE = "Remote"         # Software unlikely to contribute
    NONE = "None"            # Software cannot contribute

@dataclass
class Hazard:
    """Represents a medical device hazard"""
    hazard_id: str
    description: str
    potential_consequences: List[Dict[str, str]]
    software_contribution: Dict[str, any]
    
class SoftwareSafetyClassifier:
    """
    Automated software safety classifier for IEC 62304 compliance.
    
    This class implements algorithms to determine software safety classification
    based on hazard analysis and software contribution assessment.
    """
    
    def __init__(self, config_path: str):
        """
        Initialize the safety classifier.
        
        Args:
            config_path: Path to configuration file
        """
        self.config = self._load_config(config_path)
        self.classification_rules = self._initialize_classification_rules()
        self.audit_trail = []
        
    def _load_config(self, config_path: str) -> Dict:
        """Load configuration from YAML file"""
        try:
            with open(config_path, 'r') as f:
                config = yaml.safe_load(f)
            logger.info(f"Configuration loaded from {config_path}")
            return config
        except Exception as e:
            logger.error(f"Failed to load configuration: {e}")
            raise
            
    def _initialize_classification_rules(self) -> Dict:
        """Initialize classification decision rules"""
        return {
            'class_a_criteria': {
                'max_harm_severity': HarmSeverity.NEGLIGIBLE,
                'max_contribution': ContributionLevel.NONE,
                'description': 'Software cannot contribute to hazardous situation'
            },
            'class_b_criteria': {
                'max_harm_severity': HarmSeverity.SERIOUS,
                'max_contribution': ContributionLevel.INDIRECT,
                'description': 'Software could contribute to non-life-threatening injury'
            },
            'class_c_criteria': {
                'max_harm_severity': HarmSeverity.CATASTROPHIC,
                'max_contribution': ContributionLevel.DIRECT,
                'description': 'Software could contribute to death or serious injury'
            }
        }
    
    def classify_software_system(self, hazards_data: Dict) -> Dict:
        """
        Classify software system based on hazard analysis.
        
        Args:
            hazards_data: Dictionary containing hazard analysis data
            
        Returns:
            Dictionary containing classification results and rationale
        """
        logger.info("Starting software safety classification")
        
        # Validate input data
        self._validate_hazards_data(hazards_data)
        
        # Parse hazards
        hazards = self._parse_hazards(hazards_data)
        
        # Analyze each hazard
        hazard_classifications = []
        for hazard in hazards:
            classification = self._classify_hazard_contribution(hazard)
            hazard_classifications.append(classification)
            
        # Determine overall classification
        overall_classification = self._determine_overall_classification(hazard_classifications)
        
        # Generate classification result
        result = self._generate_classification_result(
            overall_classification,
            hazard_classifications,
            hazards_data
        )
        
        logger.info(f"Software classified as: {overall_classification.value}")
        return result
    
    def _validate_hazards_data(self, hazards_data: Dict) -> None:
        """Validate hazards data against schema"""
        schema_path = Path(__file__).parent / "schemas" / "hazards.schema.json"
        
        try:
            with open(schema_path, 'r') as f:
                schema = json.load(f)
            jsonschema.validate(hazards_data, schema)
            logger.info("Hazards data validation passed")
        except jsonschema.ValidationError as e:
            logger.error(f"Hazards data validation failed: {e}")
            raise
        except FileNotFoundError:
            logger.warning("Schema file not found, skipping validation")
    
    def _parse_hazards(self, hazards_data: Dict) -> List[Hazard]:
        """Parse hazards data into Hazard objects"""
        hazards = []
        
        for hazard_data in hazards_data.get('hazards_identified', []):
            hazard = Hazard(
                hazard_id=hazard_data['hazard_id'],
                description=hazard_data['hazard_description'],
                potential_consequences=hazard_data['potential_consequences'],
                software_contribution=hazard_data['software_contribution']
            )
            hazards.append(hazard)
            
        logger.info(f"Parsed {len(hazards)} hazards for analysis")
        return hazards
    
    def _classify_hazard_contribution(self, hazard: Hazard) -> Dict:
        """
        Classify software contribution to a specific hazard.
        
        Args:
            hazard: Hazard to analyze
            
        Returns:
            Dictionary containing hazard-specific classification
        """
        # Assess maximum harm severity
        max_severity = self._assess_maximum_harm_severity(hazard.potential_consequences)
        
        # Assess software contribution level
        contribution_level = self._assess_contribution_level(hazard.software_contribution)
        
        # Determine classification for this hazard
        hazard_classification = self._determine_hazard_classification(
            max_severity, 
            contribution_level
        )
        
        # Record audit trail
        audit_entry = {
            'hazard_id': hazard.hazard_id,
            'max_severity': max_severity.value,
            'contribution_level': contribution_level.value,
            'classification': hazard_classification.value,
            'rationale': f"Max severity: {max_severity.value}, "
                        f"Software contribution: {contribution_level.value}"
        }
        self.audit_trail.append(audit_entry)
        
        return {
            'hazard_id': hazard.hazard_id,
            'description': hazard.description,
            'max_severity': max_severity,
            'contribution_level': contribution_level,
            'classification': hazard_classification,
            'rationale': audit_entry['rationale']
        }
    
    def _assess_maximum_harm_severity(self, consequences: List[Dict]) -> HarmSeverity:
        """Assess the maximum potential harm severity"""
        max_severity = HarmSeverity.NEGLIGIBLE
        
        for consequence in consequences:
            severity_str = consequence.get('severity', 'Negligible')
            try:
                severity = HarmSeverity(severity_str)
                if self._severity_priority(severity) > self._severity_priority(max_severity):
                    max_severity = severity
            except ValueError:
                logger.warning(f"Unknown severity level: {severity_str}")
                
        return max_severity
    
    def _severity_priority(self, severity: HarmSeverity) -> int:
        """Return priority value for severity comparison"""
        priority_map = {
            HarmSeverity.NEGLIGIBLE: 0,
            HarmSeverity.MINOR: 1,
            HarmSeverity.SERIOUS: 2,
            HarmSeverity.CRITICAL: 3,
            HarmSeverity.CATASTROPHIC: 4
        }
        return priority_map.get(severity, 0)
    
    def _assess_contribution_level(self, contribution_data: Dict) -> ContributionLevel:
        """Assess software contribution level to hazard"""
        if not contribution_data.get('can_contribute', False):
            return ContributionLevel.NONE
            
        # Check contribution mechanisms
        mechanisms = contribution_data.get('contribution_mechanism', [])
        failure_prob = contribution_data.get('failure_probability', 'Low')
        
        # High-risk mechanisms indicate direct contribution
        high_risk_mechanisms = [
            'Software_Malfunction',
            'Incorrect_Algorithm',
            'Timing_Error'
        ]
        
        if any(mechanism in high_risk_mechanisms for mechanism in mechanisms):
            if failure_prob in ['High', 'Very_High']:
                return ContributionLevel.DIRECT
            else:
                return ContributionLevel.INDIRECT
        
        # Other mechanisms typically indicate indirect contribution
        if mechanisms and failure_prob not in ['Very_Low']:
            return ContributionLevel.INDIRECT
            
        return ContributionLevel.REMOTE
    
    def _determine_hazard_classification(
        self, 
        severity: HarmSeverity, 
        contribution: ContributionLevel
    ) -> SafetyClass:
        """Determine safety classification for a specific hazard"""
        
        # Class C: Death or serious injury possible
        if (severity in [HarmSeverity.CATASTROPHIC, HarmSeverity.CRITICAL] and 
            contribution in [ContributionLevel.DIRECT, ContributionLevel.INDIRECT]):
            return SafetyClass.CLASS_C
            
        # Class B: Non-life-threatening injury possible  
        if (severity == HarmSeverity.SERIOUS and 
            contribution in [ContributionLevel.DIRECT, ContributionLevel.INDIRECT]):
            return SafetyClass.CLASS_B
            
        # Conservative approach: higher class if uncertain
        if contribution == ContributionLevel.DIRECT:
            if severity in [HarmSeverity.CATASTROPHIC, HarmSeverity.CRITICAL]:
                return SafetyClass.CLASS_C
            elif severity == HarmSeverity.SERIOUS:
                return SafetyClass.CLASS_B
                
        # Class A: Cannot contribute to hazardous situations
        return SafetyClass.CLASS_A
    
    def _determine_overall_classification(
        self, 
        hazard_classifications: List[Dict]
    ) -> SafetyClass:
        """Determine overall system classification from hazard classifications"""
        
        # Overall classification is the highest individual classification
        max_classification = SafetyClass.CLASS_A
        
        for hazard_class in hazard_classifications:
            current_class = hazard_class['classification']
            if self._classification_priority(current_class) > self._classification_priority(max_classification):
                max_classification = current_class
                
        return max_classification
    
    def _classification_priority(self, classification: SafetyClass) -> int:
        """Return priority value for classification comparison"""
        priority_map = {
            SafetyClass.CLASS_A: 1,
            SafetyClass.CLASS_B: 2,
            SafetyClass.CLASS_C: 3
        }
        return priority_map.get(classification, 1)
    
    def _generate_classification_result(
        self,
        overall_classification: SafetyClass,
        hazard_classifications: List[Dict],
        original_data: Dict
    ) -> Dict:
        """Generate comprehensive classification result"""
        
        # Find contributing hazards for the overall classification
        contributing_hazards = [
            hc for hc in hazard_classifications 
            if hc['classification'] == overall_classification
        ]
        
        # Generate rationale
        rationale = self._generate_classification_rationale(
            overall_classification,
            contributing_hazards
        )
        
        result = {
            'classification_id': f"SC-{hash(str(original_data)) % 10000:04d}-001",
            'software_system': original_data.get('software_system', {}),
            'overall_classification': overall_classification.value,
            'classification_rationale': rationale,
            'hazard_analysis_summary': {
                'total_hazards_analyzed': len(hazard_classifications),
                'hazards_by_classification': self._summarize_hazards_by_classification(hazard_classifications),
                'contributing_hazards': [hc['hazard_id'] for hc in contributing_hazards]
            },
            'audit_trail': self.audit_trail,
            'classification_date': self._get_current_date(),
            'classification_tool': {
                'name': 'IEC 62304 Safety Classifier',
                'version': '1.0.0',
                'method': 'Automated hazard-based classification'
            },
            'recommendations': self._generate_recommendations(overall_classification)
        }
        
        return result
    
    def _generate_classification_rationale(
        self, 
        classification: SafetyClass,
        contributing_hazards: List[Dict]
    ) -> Dict:
        """Generate detailed rationale for classification decision"""
        
        if not contributing_hazards:
            return {
                'primary_justification': f'Software classified as {classification.value} - no hazards require higher classification',
                'contributing_hazards': [],
                'worst_case_scenario': 'No significant hazardous scenarios identified',
                'conservative_approach_applied': True
            }
        
        # Find worst-case scenario
        worst_case = max(
            contributing_hazards,
            key=lambda h: (
                self._severity_priority(h['max_severity']),
                self._contribution_priority(h['contribution_level'])
            )
        )
        
        return {
            'primary_justification': f'Software classified as {classification.value} based on potential contribution to hazardous situations',
            'contributing_hazards': [h['hazard_id'] for h in contributing_hazards],
            'worst_case_scenario': f"Hazard {worst_case['hazard_id']}: {worst_case['description']} "
                                 f"(Severity: {worst_case['max_severity'].value}, "
                                 f"Contribution: {worst_case['contribution_level'].value})",
            'conservative_approach_applied': True
        }
    
    def _contribution_priority(self, contribution: ContributionLevel) -> int:
        """Return priority value for contribution comparison"""
        priority_map = {
            ContributionLevel.NONE: 0,
            ContributionLevel.REMOTE: 1,
            ContributionLevel.INDIRECT: 2,
            ContributionLevel.DIRECT: 3
        }
        return priority_map.get(contribution, 0)
    
    def _summarize_hazards_by_classification(self, hazard_classifications: List[Dict]) -> Dict:
        """Summarize hazards by their individual classifications"""
        summary = {
            SafetyClass.CLASS_A.value: 0,
            SafetyClass.CLASS_B.value: 0,
            SafetyClass.CLASS_C.value: 0
        }
        
        for hazard_class in hazard_classifications:
            classification = hazard_class['classification'].value
            summary[classification] += 1
            
        return summary
    
    def _generate_recommendations(self, classification: SafetyClass) -> List[str]:
        """Generate recommendations based on classification"""
        recommendations = []
        
        if classification == SafetyClass.CLASS_A:
            recommendations = [
                "Implement basic software lifecycle processes",
                "Document software requirements and design",
                "Perform unit testing of software components",
                "Maintain configuration management"
            ]
        elif classification == SafetyClass.CLASS_B:
            recommendations = [
                "Implement comprehensive software lifecycle processes", 
                "Perform detailed software requirements analysis",
                "Conduct architectural and detailed design reviews",
                "Implement unit testing with statement or branch coverage",
                "Perform integration testing of software items",
                "Conduct system testing with enhanced procedures",
                "Implement comprehensive risk control measures"
            ]
        else:  # Class C
            recommendations = [
                "Implement exhaustive software lifecycle processes",
                "Perform comprehensive requirements analysis with full traceability",
                "Conduct thorough architectural and detailed design reviews",
                "Implement unit testing with statement AND branch coverage",
                "Perform comprehensive integration testing including all interfaces",
                "Conduct exhaustive system testing with all scenarios",
                "Implement comprehensive risk control measures with independent verification",
                "Consider independent software safety assessment"
            ]
            
        return recommendations
    
    def _get_current_date(self) -> str:
        """Get current date in ISO format"""
        from datetime import datetime
        return datetime.now().strftime('%Y-%m-%d')

def main():
    """Main function for command-line usage"""
    parser = argparse.ArgumentParser(
        description='IEC 62304 Software Safety Classification Tool'
    )
    parser.add_argument(
        '--config',
        type=str,
        required=True,
        help='Path to configuration file'
    )
    parser.add_argument(
        '--hazards',
        type=str,
        required=True,
        help='Path to hazards analysis JSON file'
    )
    parser.add_argument(
        '--output',
        type=str,
        default='classification_result.json',
        help='Output file for classification result'
    )
    
    args = parser.parse_args()
    
    try:
        # Initialize classifier
        classifier = SoftwareSafetyClassifier(args.config)
        
        # Load hazards data
        with open(args.hazards, 'r') as f:
            hazards_data = json.load(f)
        
        # Perform classification
        result = classifier.classify_software_system(hazards_data)
        
        # Save result
        with open(args.output, 'w') as f:
            json.dump(result, f, indent=2, default=str)
        
        print(f"Classification completed successfully!")
        print(f"Overall classification: {result['overall_classification']}")
        print(f"Results saved to: {args.output}")
        
    except Exception as e:
        logger.error(f"Classification failed: {e}")
        return 1
        
    return 0

if __name__ == '__main__':
    exit(main())