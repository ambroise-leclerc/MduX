"""
Product Realisation Automation Framework
ISO 13485:2016 Section 7 Implementation

This module provides comprehensive automation for product realisation processes
including design controls, manufacturing, and supply chain management with
AI-enhanced optimization and regulatory compliance validation.

Author: MduX Medical Device Framework
Version: 1.0.0
Compliance: ISO 13485:2016, IEC 62304, FDA 21 CFR Part 820
"""

import json
import uuid
import datetime
from typing import Dict, List, Any, Optional, Tuple
from dataclasses import dataclass, field
from enum import Enum
import asyncio
from pathlib import Path


class DeviceClassification(Enum):
    """Medical device classification levels"""
    CLASS_I = "Class I"
    CLASS_IIA = "Class IIa"  
    CLASS_IIB = "Class IIb"
    CLASS_III = "Class III"


class RiskLevel(Enum):
    """Risk assessment levels"""
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"
    CRITICAL = "critical"


class DesignPhase(Enum):
    """Design control phases"""
    PLANNING = "planning"
    INPUT = "input"
    OUTPUT = "output"
    REVIEW = "review"
    VERIFICATION = "verification"
    VALIDATION = "validation"
    TRANSFER = "transfer"
    CONTROL = "control"


@dataclass
class DeviceSpecification:
    """Device specification and requirements"""
    device_id: str
    name: str
    classification: DeviceClassification
    intended_use: str
    user_needs: List[str]
    design_inputs: List[str]
    performance_requirements: Dict[str, Any]
    safety_requirements: List[str]
    regulatory_requirements: List[str]
    risk_level: RiskLevel = RiskLevel.MEDIUM
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class DesignControl:
    """Design control tracking"""
    control_id: str
    phase: DesignPhase
    requirements: List[str]
    outputs: List[str]
    verification_methods: List[str]
    validation_methods: List[str]
    review_criteria: List[str]
    approval_status: str = "pending"
    responsible_party: str = ""
    completion_date: Optional[datetime.datetime] = None
    traceability_matrix: Dict[str, List[str]] = field(default_factory=dict)


class DesignControlsManager:
    """
    Comprehensive design controls management with AI-enhanced optimization
    Implements ISO 13485 Section 7.3 requirements
    """
    
    def __init__(self, compliance_metadata: Dict[str, Any]):
        self.compliance_metadata = compliance_metadata
        self.design_controls = {}
        self.traceability_matrix = {}
        self.ai_recommendations = {}
        
    def create_design_plan(self, device_spec: DeviceSpecification) -> Dict[str, Any]:
        """
        Create comprehensive design and development plan
        
        Args:
            device_spec: Device specification and requirements
            
        Returns:
            Comprehensive design plan with AI optimization
        """
        plan_id = f"DP-{uuid.uuid4().hex[:8]}"
        
        # AI-enhanced planning with risk-based approach
        ai_analysis = self._analyze_design_complexity(device_spec)
        
        design_plan = {
            "plan_id": plan_id,
            "device_id": device_spec.device_id,
            "planning_date": datetime.datetime.now().isoformat(),
            "design_phases": self._generate_design_phases(device_spec, ai_analysis),
            "resource_allocation": self._optimize_resource_allocation(ai_analysis),
            "timeline": self._generate_timeline(device_spec, ai_analysis),
            "risk_mitigation": self._identify_design_risks(device_spec),
            "verification_strategy": self._develop_verification_strategy(device_spec),
            "validation_strategy": self._develop_validation_strategy(device_spec),
            "review_schedule": self._schedule_design_reviews(device_spec),
            "ai_optimization": ai_analysis,
            "compliance_checkpoints": self._define_compliance_checkpoints(device_spec)
        }
        
        return design_plan
        
    def _analyze_design_complexity(self, device_spec: DeviceSpecification) -> Dict[str, Any]:
        """AI-powered design complexity analysis"""
        complexity_factors = {
            "technical_complexity": self._assess_technical_complexity(device_spec),
            "regulatory_complexity": self._assess_regulatory_complexity(device_spec),
            "integration_complexity": self._assess_integration_complexity(device_spec),
            "user_interface_complexity": self._assess_ui_complexity(device_spec),
            "manufacturing_complexity": self._assess_manufacturing_complexity(device_spec)
        }
        
        overall_complexity = sum(complexity_factors.values()) / len(complexity_factors)
        
        return {
            "complexity_factors": complexity_factors,
            "overall_complexity": overall_complexity,
            "risk_indicators": self._identify_risk_indicators(complexity_factors),
            "optimization_opportunities": self._identify_optimization_opportunities(complexity_factors),
            "resource_recommendations": self._recommend_resources(complexity_factors)
        }
        
    def _generate_design_phases(self, device_spec: DeviceSpecification, ai_analysis: Dict[str, Any]) -> List[Dict[str, Any]]:
        """Generate optimized design phases based on AI analysis"""
        base_phases = [
            {
                "phase": DesignPhase.PLANNING.value,
                "objectives": ["Define design strategy", "Allocate resources", "Establish timeline"],
                "deliverables": ["Design plan", "Resource allocation", "Risk assessment"],
                "duration_weeks": 2 + int(ai_analysis["overall_complexity"] * 2)
            },
            {
                "phase": DesignPhase.INPUT.value,
                "objectives": ["Capture user needs", "Define design inputs", "Establish acceptance criteria"],
                "deliverables": ["Design inputs specification", "User requirements", "Acceptance criteria"],
                "duration_weeks": 3 + int(ai_analysis["complexity_factors"]["technical_complexity"] * 2)
            },
            {
                "phase": DesignPhase.OUTPUT.value,
                "objectives": ["Create design outputs", "Implement solutions", "Generate documentation"],
                "deliverables": ["Design outputs", "Implementation", "Technical documentation"],
                "duration_weeks": 8 + int(ai_analysis["overall_complexity"] * 4)
            },
            {
                "phase": DesignPhase.REVIEW.value,
                "objectives": ["Systematic design review", "Risk assessment", "Compliance verification"],
                "deliverables": ["Design review report", "Risk analysis", "Compliance assessment"],
                "duration_weeks": 2 + int(ai_analysis["complexity_factors"]["regulatory_complexity"])
            },
            {
                "phase": DesignPhase.VERIFICATION.value,
                "objectives": ["Verify design meets inputs", "Validate performance", "Document evidence"],
                "deliverables": ["Verification protocols", "Test results", "Verification report"],
                "duration_weeks": 4 + int(ai_analysis["complexity_factors"]["technical_complexity"] * 2)
            },
            {
                "phase": DesignPhase.VALIDATION.value,
                "objectives": ["Validate intended use", "User testing", "Clinical evaluation"],
                "deliverables": ["Validation protocols", "User studies", "Clinical data"],
                "duration_weeks": 6 + int(ai_analysis["complexity_factors"]["regulatory_complexity"] * 2)
            }
        ]
        
        # AI-enhanced phase optimization
        for phase in base_phases:
            phase["ai_optimization"] = self._optimize_phase(phase, ai_analysis)
            
        return base_phases
        
    def manage_design_inputs(self, device_spec: DeviceSpecification) -> Dict[str, Any]:
        """
        Comprehensive design inputs management with AI enhancement
        """
        inputs_analysis = {
            "functional_requirements": self._analyze_functional_requirements(device_spec),
            "performance_requirements": self._analyze_performance_requirements(device_spec),
            "safety_requirements": self._analyze_safety_requirements(device_spec),
            "regulatory_requirements": self._analyze_regulatory_requirements(device_spec),
            "user_interface_requirements": self._analyze_ui_requirements(device_spec),
            "environmental_requirements": self._analyze_environmental_requirements(device_spec),
            "compatibility_requirements": self._analyze_compatibility_requirements(device_spec)
        }
        
        # AI-powered requirements optimization
        ai_optimization = {
            "requirements_completeness": self._assess_requirements_completeness(inputs_analysis),
            "requirements_consistency": self._check_requirements_consistency(inputs_analysis),
            "requirements_traceability": self._establish_requirements_traceability(inputs_analysis),
            "missing_requirements": self._identify_missing_requirements(inputs_analysis, device_spec),
            "conflicting_requirements": self._identify_conflicting_requirements(inputs_analysis),
            "optimization_recommendations": self._recommend_requirements_optimization(inputs_analysis)
        }
        
        return {
            "device_id": device_spec.device_id,
            "inputs_analysis": inputs_analysis,
            "ai_optimization": ai_optimization,
            "traceability_matrix": self._create_traceability_matrix(inputs_analysis),
            "approval_workflow": self._create_approval_workflow(inputs_analysis),
            "change_control": self._setup_change_control(inputs_analysis)
        }


class ManufacturingControlsManager:
    """
    Manufacturing process controls with AI-enhanced optimization
    Implements ISO 13485 Section 7.5 requirements
    """
    
    def __init__(self, compliance_metadata: Dict[str, Any]):
        self.compliance_metadata = compliance_metadata
        self.manufacturing_processes = {}
        self.control_plans = {}
        self.process_validation = {}
        
    def create_manufacturing_plan(self, device_spec: DeviceSpecification, design_outputs: Dict[str, Any]) -> Dict[str, Any]:
        """
        Create comprehensive manufacturing plan with AI optimization
        """
        plan_id = f"MP-{uuid.uuid4().hex[:8]}"
        
        # AI-powered manufacturing analysis
        manufacturing_analysis = self._analyze_manufacturing_requirements(device_spec, design_outputs)
        
        manufacturing_plan = {
            "plan_id": plan_id,
            "device_id": device_spec.device_id,
            "manufacturing_processes": self._define_manufacturing_processes(manufacturing_analysis),
            "process_controls": self._establish_process_controls(manufacturing_analysis),
            "quality_controls": self._design_quality_controls(manufacturing_analysis),
            "environmental_controls": self._specify_environmental_controls(manufacturing_analysis),
            "personnel_requirements": self._define_personnel_requirements(manufacturing_analysis),
            "equipment_requirements": self._specify_equipment_requirements(manufacturing_analysis),
            "validation_requirements": self._establish_validation_requirements(manufacturing_analysis),
            "monitoring_systems": self._design_monitoring_systems(manufacturing_analysis),
            "ai_optimization": manufacturing_analysis["ai_optimization"]
        }
        
        return manufacturing_plan
        
    def implement_process_validation(self, manufacturing_plan: Dict[str, Any]) -> Dict[str, Any]:
        """
        Comprehensive process validation with AI-enhanced protocols
        """
        validation_id = f"PV-{uuid.uuid4().hex[:8]}"
        
        validation_framework = {
            "validation_id": validation_id,
            "device_id": manufacturing_plan["device_id"],
            "validation_strategy": self._develop_validation_strategy(manufacturing_plan),
            "installation_qualification": self._create_iq_protocols(manufacturing_plan),
            "operational_qualification": self._create_oq_protocols(manufacturing_plan),
            "performance_qualification": self._create_pq_protocols(manufacturing_plan),
            "process_capability_studies": self._design_capability_studies(manufacturing_plan),
            "statistical_validation": self._implement_statistical_validation(manufacturing_plan),
            "ai_enhanced_monitoring": self._setup_ai_monitoring(manufacturing_plan),
            "continuous_verification": self._establish_continuous_verification(manufacturing_plan)
        }
        
        return validation_framework
        
    def _analyze_manufacturing_requirements(self, device_spec: DeviceSpecification, design_outputs: Dict[str, Any]) -> Dict[str, Any]:
        """AI-powered manufacturing requirements analysis"""
        return {
            "process_complexity": self._assess_process_complexity(device_spec, design_outputs),
            "quality_requirements": self._analyze_quality_requirements(device_spec),
            "environmental_requirements": self._analyze_environmental_requirements(device_spec),
            "scalability_requirements": self._assess_scalability_needs(device_spec),
            "automation_opportunities": self._identify_automation_opportunities(device_spec),
            "ai_optimization": {
                "process_optimization": self._optimize_processes(device_spec),
                "quality_prediction": self._predict_quality_issues(device_spec),
                "efficiency_improvements": self._recommend_efficiency_improvements(device_spec),
                "cost_optimization": self._optimize_costs(device_spec)
            }
        }


class SupplyChainManager:
    """
    AI-enhanced supply chain management for medical devices
    Implements ISO 13485 purchasing and supplier controls
    """
    
    def __init__(self, compliance_metadata: Dict[str, Any]):
        self.compliance_metadata = compliance_metadata
        self.suppliers = {}
        self.supplier_evaluations = {}
        self.purchasing_controls = {}
        
    def evaluate_supplier(self, supplier_info: Dict[str, Any]) -> Dict[str, Any]:
        """
        Comprehensive supplier evaluation with AI risk assessment
        """
        evaluation_id = f"SE-{uuid.uuid4().hex[:8]}"
        
        # AI-powered supplier risk assessment
        risk_assessment = self._assess_supplier_risk(supplier_info)
        
        evaluation = {
            "evaluation_id": evaluation_id,
            "supplier_id": supplier_info["supplier_id"],
            "evaluation_date": datetime.datetime.now().isoformat(),
            "capability_assessment": self._assess_supplier_capability(supplier_info),
            "quality_system_assessment": self._assess_quality_system(supplier_info),
            "regulatory_compliance": self._verify_regulatory_compliance(supplier_info),
            "financial_stability": self._assess_financial_stability(supplier_info),
            "risk_assessment": risk_assessment,
            "performance_history": self._analyze_performance_history(supplier_info),
            "improvement_recommendations": self._recommend_improvements(supplier_info, risk_assessment),
            "approval_status": self._determine_approval_status(risk_assessment),
            "monitoring_requirements": self._define_monitoring_requirements(risk_assessment),
            "ai_insights": risk_assessment["ai_insights"]
        }
        
        return evaluation
        
    def create_purchasing_controls(self, supplier_evaluation: Dict[str, Any], product_requirements: Dict[str, Any]) -> Dict[str, Any]:
        """
        Create comprehensive purchasing controls with AI optimization
        """
        controls_id = f"PC-{uuid.uuid4().hex[:8]}"
        
        purchasing_controls = {
            "controls_id": controls_id,
            "supplier_id": supplier_evaluation["supplier_id"],
            "product_specifications": self._define_product_specifications(product_requirements),
            "quality_requirements": self._specify_quality_requirements(product_requirements),
            "acceptance_criteria": self._establish_acceptance_criteria(product_requirements),
            "inspection_requirements": self._define_inspection_requirements(product_requirements),
            "documentation_requirements": self._specify_documentation_requirements(product_requirements),
            "delivery_requirements": self._establish_delivery_requirements(product_requirements),
            "change_control": self._setup_supplier_change_control(supplier_evaluation),
            "performance_monitoring": self._create_performance_monitoring(supplier_evaluation),
            "ai_optimization": self._optimize_purchasing_process(supplier_evaluation, product_requirements)
        }
        
        return purchasing_controls


class ProductRealizationOrchestrator:
    """
    Master orchestrator for comprehensive product realisation automation
    Coordinates all aspects of ISO 13485 Section 7 implementation
    """
    
    def __init__(self, compliance_metadata: Dict[str, Any]):
        self.compliance_metadata = compliance_metadata
        self.design_manager = DesignControlsManager(compliance_metadata)
        self.manufacturing_manager = ManufacturingControlsManager(compliance_metadata)
        self.supply_chain_manager = SupplyChainManager(compliance_metadata)
        
    async def orchestrate_product_realization(self, device_spec: DeviceSpecification) -> Dict[str, Any]:
        """
        Complete product realisation orchestration with parallel processing
        """
        orchestration_id = f"PR-{uuid.uuid4().hex[:8]}"
        
        # Phase 1: Design Planning and Requirements (Parallel where possible)
        design_plan_task = asyncio.create_task(
            self._async_design_planning(device_spec)
        )
        
        supply_chain_setup_task = asyncio.create_task(
            self._async_supply_chain_setup(device_spec)
        )
        
        design_plan, supply_chain_setup = await asyncio.gather(
            design_plan_task, supply_chain_setup_task
        )
        
        # Phase 2: Design Implementation
        design_implementation = await self._async_design_implementation(device_spec, design_plan)
        
        # Phase 3: Manufacturing Planning (can start in parallel with design validation)
        manufacturing_planning_task = asyncio.create_task(
            self._async_manufacturing_planning(device_spec, design_implementation["design_outputs"])
        )
        
        design_validation_task = asyncio.create_task(
            self._async_design_validation(design_implementation)
        )
        
        manufacturing_plan, design_validation = await asyncio.gather(
            manufacturing_planning_task, design_validation_task
        )
        
        # Phase 4: Manufacturing Implementation and Process Validation
        manufacturing_implementation = await self._async_manufacturing_implementation(manufacturing_plan)
        
        # Generate comprehensive orchestration report
        orchestration_report = {
            "orchestration_id": orchestration_id,
            "device_id": device_spec.device_id,
            "orchestration_date": datetime.datetime.now().isoformat(),
            "phases_completed": [
                "Design Planning",
                "Supply Chain Setup", 
                "Design Implementation",
                "Design Validation",
                "Manufacturing Planning",
                "Manufacturing Implementation"
            ],
            "design_summary": {
                "design_plan_id": design_plan["plan_id"],
                "design_controls": len(design_implementation.get("design_controls", [])),
                "verification_activities": len(design_validation.get("verification_activities", [])),
                "validation_activities": len(design_validation.get("validation_activities", []))
            },
            "manufacturing_summary": {
                "manufacturing_plan_id": manufacturing_plan["plan_id"],
                "processes_defined": len(manufacturing_plan.get("manufacturing_processes", [])),
                "control_points": len(manufacturing_implementation.get("control_points", [])),
                "validation_protocols": len(manufacturing_implementation.get("validation_protocols", []))
            },
            "supply_chain_summary": {
                "suppliers_evaluated": len(supply_chain_setup.get("supplier_evaluations", [])),
                "purchasing_controls": len(supply_chain_setup.get("purchasing_controls", [])),
                "supply_chain_risks": supply_chain_setup.get("risk_assessment", {})
            },
            "compliance_status": self._assess_overall_compliance(
                design_validation, manufacturing_implementation, supply_chain_setup
            ),
            "performance_metrics": self._calculate_performance_metrics(
                design_implementation, manufacturing_implementation, supply_chain_setup
            ),
            "recommendations": self._generate_improvement_recommendations(
                design_validation, manufacturing_implementation, supply_chain_setup
            ),
            "next_steps": self._define_next_steps(orchestration_report)
        }
        
        return orchestration_report
        
    # Async implementation methods (simplified for brevity)
    async def _async_design_planning(self, device_spec: DeviceSpecification) -> Dict[str, Any]:
        """Async design planning implementation"""
        return self.design_manager.create_design_plan(device_spec)
        
    async def _async_supply_chain_setup(self, device_spec: DeviceSpecification) -> Dict[str, Any]:
        """Async supply chain setup implementation"""
        # Simulate async supply chain analysis and setup
        await asyncio.sleep(0.1)  # Simulate async processing
        return {"setup_complete": True, "suppliers_identified": 5, "risk_assessment": {"overall_risk": "low"}}
        
    async def _async_design_implementation(self, device_spec: DeviceSpecification, design_plan: Dict[str, Any]) -> Dict[str, Any]:
        """Async design implementation"""
        design_inputs = self.design_manager.manage_design_inputs(device_spec)
        return {"design_inputs": design_inputs, "design_outputs": {"outputs_created": True}}
        
    async def _async_design_validation(self, design_implementation: Dict[str, Any]) -> Dict[str, Any]:
        """Async design validation"""
        await asyncio.sleep(0.1)  # Simulate validation processing
        return {"verification_activities": ["V1", "V2"], "validation_activities": ["Val1", "Val2"]}
        
    async def _async_manufacturing_planning(self, device_spec: DeviceSpecification, design_outputs: Dict[str, Any]) -> Dict[str, Any]:
        """Async manufacturing planning"""
        return self.manufacturing_manager.create_manufacturing_plan(device_spec, design_outputs)
        
    async def _async_manufacturing_implementation(self, manufacturing_plan: Dict[str, Any]) -> Dict[str, Any]:
        """Async manufacturing implementation"""
        process_validation = self.manufacturing_manager.implement_process_validation(manufacturing_plan)
        return {"process_validation": process_validation, "control_points": ["CP1", "CP2"], "validation_protocols": ["VP1", "VP2"]}


# Utility functions for AI enhancement (simplified implementations)
def _assess_technical_complexity(self, device_spec: DeviceSpecification) -> float:
    """Assess technical complexity based on device characteristics"""
    complexity_score = 0.3  # Base complexity
    
    if device_spec.classification in [DeviceClassification.CLASS_IIB, DeviceClassification.CLASS_III]:
        complexity_score += 0.3
    if device_spec.risk_level in [RiskLevel.HIGH, RiskLevel.CRITICAL]:
        complexity_score += 0.2
    if len(device_spec.design_inputs) > 10:
        complexity_score += 0.2
        
    return min(complexity_score, 1.0)

def _assess_regulatory_complexity(self, device_spec: DeviceSpecification) -> float:
    """Assess regulatory complexity"""
    complexity_score = 0.2  # Base regulatory complexity
    
    if len(device_spec.regulatory_requirements) > 5:
        complexity_score += 0.3
    if device_spec.classification == DeviceClassification.CLASS_III:
        complexity_score += 0.4
    if "FDA" in str(device_spec.regulatory_requirements):
        complexity_score += 0.1
        
    return min(complexity_score, 1.0)


# Additional helper methods would be implemented here
# For brevity, showing the structure and key methods

if __name__ == "__main__":
    # Example usage demonstration
    import asyncio
    
    async def main():
        # Example device specification
        device_spec = DeviceSpecification(
            device_id="MDX-2024-001",
            name="Advanced Medical Monitoring System",
            classification=DeviceClassification.CLASS_IIB,
            intended_use="Continuous patient monitoring in clinical settings",
            user_needs=["Real-time monitoring", "Alert systems", "Data logging"],
            design_inputs=["User interface requirements", "Performance specifications", "Safety requirements"],
            performance_requirements={"accuracy": ">95%", "response_time": "<2s"},
            safety_requirements=["Alarm systems", "Fail-safe operation", "EMC compliance"],
            regulatory_requirements=["FDA 510(k)", "CE marking", "ISO 14155"],
            risk_level=RiskLevel.HIGH
        )
        
        # Initialize orchestrator
        compliance_metadata = {
            "standard": "ISO 13485:2016",
            "device_class": "Class IIB",
            "regulatory_pathway": "FDA 510(k)"
        }
        
        orchestrator = ProductRealizationOrchestrator(compliance_metadata)
        
        # Execute complete product realisation
        result = await orchestrator.orchestrate_product_realization(device_spec)
        
        print(f"Product Realisation Orchestration Complete: {result['orchestration_id']}")
        print(f"Phases Completed: {len(result['phases_completed'])}")
        print(f"Overall Compliance Status: {result['compliance_status']}")
        
    # Run the example
    asyncio.run(main())