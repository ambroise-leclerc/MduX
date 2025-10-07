#!/usr/bin/env python3
"""
ISO 13485 Quality Management System Automation Framework

This module provides comprehensive AI-enhanced automation for ISO 13485 quality 
management system implementation, including scope assessment, process optimization,
and compliance monitoring.

Usage:
    python qms_automation.py --config qms_config.yaml --organization org_profile.json
    
Requirements:
    - Python 3.9+
    - PyYAML
    - jsonschema
    - pandas
    - scikit-learn
    - networkx
"""

import json
import yaml
import logging
import argparse
import pandas as pd
import networkx as nx
from typing import Dict, List, Optional, Tuple, Any
from dataclasses import dataclass, asdict
from enum import Enum
from pathlib import Path
from datetime import datetime, timedelta
import jsonschema
from sklearn.cluster import KMeansCluster
from sklearn.metrics import silhouette_score

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class OrganizationRole(Enum):
    """Organization roles in medical device value chain"""
    MANUFACTURER = "manufacturer"
    AUTHORISED_REPRESENTATIVE = "authorised_representative"
    IMPORTER = "importer"
    DISTRIBUTOR = "distributor"

class DeviceClassification(Enum):
    """Medical device risk classifications"""
    CLASS_I = "Class_I"
    CLASS_IIA = "Class_IIa"
    CLASS_IIB = "Class_IIb"
    CLASS_III = "Class_III"

class AutomationLevel(Enum):
    """AI automation capability levels"""
    MANUAL = "manual"
    ASSISTED = "assisted"
    AUTOMATED = "automated"
    INTELLIGENT = "intelligent"
    AUTONOMOUS = "autonomous"

@dataclass
class OrganizationProfile:
    """Comprehensive organization profile for QMS tailoring"""
    name: str
    role: OrganizationRole
    size: str
    regulatory_jurisdictions: List[str]
    device_portfolio: List[Dict[str, Any]]
    annual_revenue: Optional[float] = None
    employee_count: Optional[int] = None
    quality_maturity_level: Optional[str] = None

@dataclass
class ProcessDefinition:
    """Process definition with AI enhancement capabilities"""
    process_name: str
    owner: str
    inputs: List[str]
    outputs: List[str]
    controls: List[str]
    resources: List[str]
    performance_indicators: List[str]
    automation_level: AutomationLevel
    ai_enhancement_potential: float

class QMSScopeAssessor:
    """
    AI-powered QMS scope assessment and requirements determination
    """
    
    def __init__(self, regulatory_database_path: Optional[str] = None):
        self.regulatory_requirements = {}
        self.exclusion_rules = {}
        self.scope_templates = {}
        self.load_regulatory_database(regulatory_database_path)
    
    def load_regulatory_database(self, database_path: Optional[str]):
        """Load regulatory requirements database"""
        if database_path and Path(database_path).exists():
            with open(database_path, 'r') as f:
                self.regulatory_requirements = json.load(f)
        else:
            # Default regulatory requirements
            self.regulatory_requirements = {
                "FDA": {
                    "applicable_roles": ["manufacturer", "importer"],
                    "required_clauses": ["4.1", "4.2", "5", "6", "7", "8"],
                    "design_control_exemptions": ["510k_exempt", "predicate_device"],
                    "special_requirements": ["medical_device_reporting", "quality_system_regulation"]
                },
                "CE": {
                    "applicable_roles": ["manufacturer", "authorised_representative", "importer"],
                    "required_clauses": ["4.1", "4.2", "5", "6", "7", "8"],
                    "design_control_exemptions": [],
                    "special_requirements": ["mdr_compliance", "notified_body_involvement"]
                }
            }
    
    def assess_scope(self, org_profile: OrganizationProfile) -> Dict[str, Any]:
        """
        Comprehensive AI-powered scope assessment
        """
        logger.info(f"Starting QMS scope assessment for {org_profile.name}")
        
        scope_assessment = {
            "organization_analysis": self.analyze_organization(org_profile),
            "regulatory_analysis": self.analyze_regulatory_requirements(org_profile),
            "device_portfolio_analysis": self.analyze_device_portfolio(org_profile),
            "scope_determination": self.determine_scope(org_profile),
            "exclusion_assessment": self.assess_exclusions(org_profile),
            "implementation_complexity": self.assess_complexity(org_profile)
        }
        
        # Generate scope statement
        scope_assessment["scope_statement"] = self.generate_scope_statement(scope_assessment)
        
        logger.info("QMS scope assessment completed successfully")
        return scope_assessment
    
    def analyze_organization(self, org_profile: OrganizationProfile) -> Dict[str, Any]:
        """Analyze organization characteristics for QMS tailoring"""
        analysis = {
            "role_classification": org_profile.role.value,
            "size_impact": self.assess_size_impact(org_profile),
            "maturity_assessment": self.assess_quality_maturity(org_profile),
            "resource_capacity": self.assess_resource_capacity(org_profile),
            "automation_readiness": self.assess_automation_readiness(org_profile)
        }
        
        return analysis
    
    def analyze_regulatory_requirements(self, org_profile: OrganizationProfile) -> Dict[str, Any]:
        """AI-powered regulatory requirements analysis"""
        regulatory_analysis = {}
        
        for jurisdiction in org_profile.regulatory_jurisdictions:
            if jurisdiction in self.regulatory_requirements:
                requirements = self.regulatory_requirements[jurisdiction]
                
                regulatory_analysis[jurisdiction] = {
                    "applicability": org_profile.role.value in requirements["applicable_roles"],
                    "required_clauses": requirements["required_clauses"],
                    "special_requirements": requirements["special_requirements"],
                    "design_control_requirements": self.assess_design_control_requirements(
                        org_profile, jurisdiction
                    ),
                    "compliance_complexity": self.assess_compliance_complexity(
                        org_profile, jurisdiction
                    )
                }
        
        return regulatory_analysis
    
    def analyze_device_portfolio(self, org_profile: OrganizationProfile) -> Dict[str, Any]:
        """Comprehensive device portfolio analysis for QMS requirements"""
        portfolio_analysis = {
            "device_count": len(org_profile.device_portfolio),
            "classification_distribution": self.analyze_classification_distribution(org_profile),
            "special_category_requirements": self.analyze_special_requirements(org_profile),
            "risk_assessment": self.assess_portfolio_risk(org_profile),
            "complexity_scoring": self.score_portfolio_complexity(org_profile)
        }
        
        return portfolio_analysis
    
    def determine_scope(self, org_profile: OrganizationProfile) -> Dict[str, Any]:
        """AI-powered scope determination with optimization"""
        base_scope = self.determine_base_scope(org_profile)
        optimized_scope = self.optimize_scope_for_efficiency(base_scope, org_profile)
        
        scope_determination = {
            "base_scope": base_scope,
            "optimized_scope": optimized_scope,
            "scope_rationale": self.generate_scope_rationale(optimized_scope),
            "implementation_priorities": self.prioritize_implementation(optimized_scope),
            "automation_opportunities": self.identify_automation_opportunities(optimized_scope)
        }
        
        return scope_determination
    
    def generate_scope_statement(self, assessment: Dict[str, Any]) -> str:
        """Generate formal QMS scope statement"""
        org_analysis = assessment["organization_analysis"]
        scope_determination = assessment["scope_determination"]["optimized_scope"]
        
        scope_statement = f"""
        ISO 13485:2016 Quality Management System Scope Statement
        
        Organization: {assessment.get('organization_name', 'Medical Device Organization')}
        Role: {org_analysis['role_classification'].replace('_', ' ').title()}
        
        This quality management system applies to:
        
        Products and Services:
        {self._format_product_scope(scope_determination.get('product_scope', []))}
        
        Lifecycle Activities:
        {self._format_lifecycle_scope(scope_determination.get('lifecycle_scope', []))}
        
        Regulatory Context:
        {self._format_regulatory_scope(assessment['regulatory_analysis'])}
        
        Exclusions:
        {self._format_exclusions(assessment.get('exclusion_assessment', {}))}
        
        This scope ensures compliance with customer requirements and applicable
        regulatory requirements while maintaining focus on patient safety and
        product quality throughout the medical device lifecycle.
        
        Automation Level: {scope_determination.get('target_automation_level', 'Assisted')}
        Implementation Priority: {scope_determination.get('implementation_priority', 'Standard')}
        """
        
        return scope_statement.strip()

class ProcessArchitectureOptimizer:
    """
    AI-powered process architecture optimization and design system
    """
    
    def __init__(self):
        self.process_templates = {}
        self.optimization_algorithms = {}
        self.interaction_patterns = {}
        self.load_process_templates()
    
    def load_process_templates(self):
        """Load standard process templates and patterns"""
        self.process_templates = {
            "management_processes": [
                ProcessDefinition(
                    "Management Commitment",
                    "CEO",
                    ["strategic_objectives", "regulatory_requirements"],
                    ["quality_policy", "resource_allocation"],
                    ["board_oversight", "performance_reviews"],
                    ["executive_team", "quality_budget"],
                    ["commitment_evidence", "communication_effectiveness"],
                    AutomationLevel.ASSISTED,
                    0.7
                ),
                ProcessDefinition(
                    "Management Review",
                    "Quality Manager",
                    ["performance_data", "audit_results", "customer_feedback"],
                    ["management_decisions", "resource_allocations", "improvement_actions"],
                    ["review_procedures", "decision_criteria"],
                    ["management_team", "data_systems"],
                    ["review_effectiveness", "decision_implementation"],
                    AutomationLevel.AUTOMATED,
                    0.9
                )
            ],
            "realization_processes": [
                ProcessDefinition(
                    "Design Controls",
                    "Design Manager",
                    ["user_needs", "regulatory_requirements", "risk_analysis"],
                    ["design_specifications", "verification_results", "validation_evidence"],
                    ["design_procedures", "review_gates", "change_control"],
                    ["design_team", "testing_facilities", "regulatory_expertise"],
                    ["design_quality", "time_to_market", "regulatory_compliance"],
                    AutomationLevel.INTELLIGENT,
                    0.8
                )
            ]
        }
    
    def optimize_process_architecture(self, org_profile: OrganizationProfile, 
                                    scope_assessment: Dict[str, Any]) -> Dict[str, Any]:
        """
        AI-powered process architecture optimization
        """
        logger.info("Starting process architecture optimization")
        
        # Generate base architecture
        base_architecture = self.generate_base_architecture(org_profile, scope_assessment)
        
        # Optimize for efficiency
        optimized_architecture = self.optimize_for_efficiency(base_architecture, org_profile)
        
        # Optimize for automation
        ai_enhanced_architecture = self.enhance_with_ai(optimized_architecture, org_profile)
        
        # Validate architecture
        validation_results = self.validate_architecture(ai_enhanced_architecture)
        
        architecture_optimization = {
            "base_architecture": base_architecture,
            "optimized_architecture": optimized_architecture,
            "ai_enhanced_architecture": ai_enhanced_architecture,
            "validation_results": validation_results,
            "implementation_roadmap": self.create_implementation_roadmap(ai_enhanced_architecture),
            "performance_predictions": self.predict_performance(ai_enhanced_architecture)
        }
        
        logger.info("Process architecture optimization completed")
        return architecture_optimization
    
    def generate_base_architecture(self, org_profile: OrganizationProfile, 
                                 scope_assessment: Dict[str, Any]) -> Dict[str, Any]:
        """Generate base process architecture from templates"""
        architecture = {
            "core_processes": {},
            "supporting_processes": {},
            "process_interactions": [],
            "resource_requirements": {}
        }
        
        # Select and customize processes based on scope
        scope_requirements = scope_assessment["scope_determination"]["optimized_scope"]
        
        for process_category, processes in self.process_templates.items():
            architecture["core_processes"][process_category] = []
            
            for process_template in processes:
                # Customize process for organization
                customized_process = self.customize_process(process_template, org_profile)
                architecture["core_processes"][process_category].append(customized_process)
        
        # Define process interactions
        architecture["process_interactions"] = self.define_process_interactions(
            architecture["core_processes"]
        )
        
        return architecture
    
    def optimize_for_efficiency(self, architecture: Dict[str, Any], 
                               org_profile: OrganizationProfile) -> Dict[str, Any]:
        """Optimize architecture for operational efficiency"""
        optimized_architecture = architecture.copy()
        
        # Identify optimization opportunities
        optimization_opportunities = self.identify_efficiency_opportunities(architecture)
        
        # Apply optimizations
        for opportunity in optimization_opportunities:
            if opportunity["type"] == "process_consolidation":
                optimized_architecture = self.consolidate_processes(
                    optimized_architecture, opportunity["details"]
                )
            elif opportunity["type"] == "automation_enhancement":
                optimized_architecture = self.enhance_automation(
                    optimized_architecture, opportunity["details"]
                )
            elif opportunity["type"] == "resource_optimization":
                optimized_architecture = self.optimize_resources(
                    optimized_architecture, opportunity["details"]
                )
        
        return optimized_architecture
    
    def enhance_with_ai(self, architecture: Dict[str, Any], 
                       org_profile: OrganizationProfile) -> Dict[str, Any]:
        """Enhance architecture with AI capabilities"""
        ai_enhanced = architecture.copy()
        
        # Identify AI enhancement opportunities
        ai_opportunities = self.identify_ai_opportunities(architecture, org_profile)
        
        # Apply AI enhancements
        for process_category in ai_enhanced["core_processes"]:
            for i, process in enumerate(ai_enhanced["core_processes"][process_category]):
                # Determine optimal automation level
                target_automation = self.determine_target_automation(process, org_profile)
                
                # Enhance process with AI capabilities
                enhanced_process = self.enhance_process_with_ai(process, target_automation)
                
                ai_enhanced["core_processes"][process_category][i] = enhanced_process
        
        # Add AI infrastructure requirements
        ai_enhanced["ai_infrastructure"] = self.design_ai_infrastructure(ai_enhanced)
        
        return ai_enhanced

class ComplianceMonitoringSystem:
    """
    AI-powered compliance monitoring and validation system
    """
    
    def __init__(self):
        self.compliance_rules = {}
        self.monitoring_algorithms = {}
        self.validation_frameworks = {}
        self.setup_compliance_framework()
    
    def setup_compliance_framework(self):
        """Setup comprehensive compliance monitoring framework"""
        self.compliance_rules = {
            "iso_13485_requirements": self.load_iso_requirements(),
            "regulatory_mappings": self.load_regulatory_mappings(),
            "validation_criteria": self.load_validation_criteria()
        }
    
    def monitor_qms_compliance(self, qms_implementation: Dict[str, Any]) -> Dict[str, Any]:
        """
        Real-time QMS compliance monitoring and assessment
        """
        logger.info("Starting QMS compliance monitoring")
        
        compliance_assessment = {
            "requirement_coverage": self.assess_requirement_coverage(qms_implementation),
            "process_compliance": self.assess_process_compliance(qms_implementation),
            "documentation_compliance": self.assess_documentation_compliance(qms_implementation),
            "performance_compliance": self.assess_performance_compliance(qms_implementation),
            "regulatory_compliance": self.assess_regulatory_compliance(qms_implementation),
            "overall_score": 0.0,
            "improvement_recommendations": []
        }
        
        # Calculate overall compliance score
        compliance_assessment["overall_score"] = self.calculate_compliance_score(
            compliance_assessment
        )
        
        # Generate improvement recommendations
        compliance_assessment["improvement_recommendations"] = (
            self.generate_improvement_recommendations(compliance_assessment)
        )
        
        logger.info(f"QMS compliance score: {compliance_assessment['overall_score']:.2f}")
        return compliance_assessment
    
    def assess_requirement_coverage(self, qms_implementation: Dict[str, Any]) -> Dict[str, Any]:
        """Assess coverage of ISO 13485 requirements"""
        requirements = self.compliance_rules["iso_13485_requirements"]
        coverage_assessment = {}
        
        for clause, requirement_details in requirements.items():
            coverage_assessment[clause] = {
                "implemented": self.check_requirement_implementation(
                    clause, qms_implementation
                ),
                "evidence_quality": self.assess_evidence_quality(
                    clause, qms_implementation
                ),
                "effectiveness_score": self.score_requirement_effectiveness(
                    clause, qms_implementation
                ),
                "improvement_opportunities": self.identify_requirement_improvements(
                    clause, qms_implementation
                )
            }
        
        return coverage_assessment
    
    def generate_compliance_dashboard(self, compliance_data: Dict[str, Any]) -> Dict[str, Any]:
        """Generate comprehensive compliance monitoring dashboard"""
        dashboard = {
            "executive_summary": self.create_executive_summary(compliance_data),
            "compliance_metrics": self.create_compliance_metrics(compliance_data),
            "trend_analysis": self.perform_trend_analysis(compliance_data),
            "risk_indicators": self.identify_risk_indicators(compliance_data),
            "action_items": self.prioritize_action_items(compliance_data),
            "performance_predictions": self.predict_compliance_performance(compliance_data)
        }
        
        return dashboard

class QMSAutomationOrchestrator:
    """
    Master orchestrator for QMS automation implementation
    """
    
    def __init__(self, config_path: str):
        self.config = self.load_config(config_path)
        self.scope_assessor = QMSScopeAssessor()
        self.process_optimizer = ProcessArchitectureOptimizer()
        self.compliance_monitor = ComplianceMonitoringSystem()
        self.implementation_tracker = {}
    
    def load_config(self, config_path: str) -> Dict[str, Any]:
        """Load configuration from YAML file"""
        with open(config_path, 'r') as f:
            return yaml.safe_load(f)
    
    def orchestrate_qms_implementation(self, org_profile: OrganizationProfile) -> Dict[str, Any]:
        """
        Orchestrate complete QMS implementation with AI optimization
        """
        logger.info(f"Starting QMS implementation orchestration for {org_profile.name}")
        
        implementation_results = {
            "scope_assessment": self.scope_assessor.assess_scope(org_profile),
            "process_architecture": None,
            "compliance_framework": None,
            "implementation_plan": None,
            "automation_configuration": None,
            "performance_monitoring": None
        }
        
        # Process architecture optimization
        implementation_results["process_architecture"] = (
            self.process_optimizer.optimize_process_architecture(
                org_profile, implementation_results["scope_assessment"]
            )
        )
        
        # Compliance framework setup
        implementation_results["compliance_framework"] = (
            self.setup_compliance_framework(
                org_profile, implementation_results["process_architecture"]
            )
        )
        
        # Implementation planning
        implementation_results["implementation_plan"] = (
            self.create_implementation_plan(implementation_results)
        )
        
        # Automation configuration
        implementation_results["automation_configuration"] = (
            self.configure_automation_systems(implementation_results)
        )
        
        # Performance monitoring setup
        implementation_results["performance_monitoring"] = (
            self.setup_performance_monitoring(implementation_results)
        )
        
        logger.info("QMS implementation orchestration completed successfully")
        return implementation_results
    
    def create_implementation_plan(self, implementation_results: Dict[str, Any]) -> Dict[str, Any]:
        """Create comprehensive implementation plan with AI optimization"""
        plan = {
            "phases": self.define_implementation_phases(implementation_results),
            "timeline": self.optimize_implementation_timeline(implementation_results),
            "resource_requirements": self.calculate_resource_requirements(implementation_results),
            "risk_mitigation": self.plan_risk_mitigation(implementation_results),
            "success_criteria": self.define_success_criteria(implementation_results),
            "monitoring_framework": self.design_monitoring_framework(implementation_results)
        }
        
        return plan
    
    def monitor_implementation_progress(self, implementation_id: str) -> Dict[str, Any]:
        """Real-time implementation progress monitoring"""
        if implementation_id not in self.implementation_tracker:
            raise ValueError(f"Implementation {implementation_id} not found")
        
        implementation_data = self.implementation_tracker[implementation_id]
        
        progress_report = {
            "overall_progress": self.calculate_overall_progress(implementation_data),
            "phase_progress": self.calculate_phase_progress(implementation_data),
            "milestone_status": self.assess_milestone_status(implementation_data),
            "risk_assessment": self.assess_implementation_risks(implementation_data),
            "performance_indicators": self.calculate_performance_indicators(implementation_data),
            "recommendations": self.generate_implementation_recommendations(implementation_data)
        }
        
        return progress_report

def main():
    """Main function for command-line usage"""
    parser = argparse.ArgumentParser(
        description='ISO 13485 QMS Automation Framework'
    )
    parser.add_argument(
        '--config',
        type=str,
        required=True,
        help='Path to configuration file'
    )
    parser.add_argument(
        '--organization',
        type=str,
        required=True,
        help='Path to organization profile JSON file'
    )
    parser.add_argument(
        '--output',
        type=str,
        default='qms_implementation_results.json',
        help='Output file for implementation results'
    )
    
    args = parser.parse_args()
    
    try:
        # Load organization profile
        with open(args.organization, 'r') as f:
            org_data = json.load(f)
        
        org_profile = OrganizationProfile(
            name=org_data['name'],
            role=OrganizationRole(org_data['role']),
            size=org_data['size'],
            regulatory_jurisdictions=org_data['regulatory_jurisdictions'],
            device_portfolio=org_data['device_portfolio'],
            annual_revenue=org_data.get('annual_revenue'),
            employee_count=org_data.get('employee_count'),
            quality_maturity_level=org_data.get('quality_maturity_level')
        )
        
        # Initialize orchestrator
        orchestrator = QMSAutomationOrchestrator(args.config)
        
        # Execute QMS implementation
        results = orchestrator.orchestrate_qms_implementation(org_profile)
        
        # Save results
        with open(args.output, 'w') as f:
            json.dump(results, f, indent=2, default=str)
        
        print(f"QMS implementation orchestration completed successfully!")
        print(f"Results saved to: {args.output}")
        
        # Print summary
        scope_assessment = results['scope_assessment']
        print(f"\nImplementation Summary:")
        print(f"Organization: {org_profile.name}")
        print(f"Role: {org_profile.role.value}")
        print(f"Applicable Requirements: {len(scope_assessment.get('scope_determination', {}).get('optimized_scope', {}).get('required_clauses', []))}")
        print(f"Implementation Complexity: {scope_assessment.get('implementation_complexity', 'Unknown')}")
        
    except Exception as e:
        logger.error(f"QMS implementation failed: {e}")
        return 1
        
    return 0

if __name__ == '__main__':
    exit(main())